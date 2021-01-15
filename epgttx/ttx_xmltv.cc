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
 * Copyright 2006-2011,2020 by T. Zoerner (tomzo at users.sf.net)
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <string>
#include <algorithm>
#include <regex>

using namespace std;

#include "ttx_db.h"
#include "ttx_util.h"
#include "ttx_scrape.h"
#include "ttx_xmltv.h"

/* ------------------------------------------------------------------------------
 * Convert a UNIX epoch timestamp into XMLTV format
 */
string GetXmlTimestamp(time_t whence)
{
   // get GMT in broken-down format
   struct tm * ptm = gmtime(&whence);

   char buf[100];
   strftime(buf, sizeof(buf), "%Y%m%d%H%M%S +0000", ptm);

   return string(buf);
}

/* ------------------------------------------------------------------------------
 * Determine LTO for a given time and date
 * - LTO depends on the local time zone and daylight saving time being in effect or not
 */
int DetermineLto(time_t whence)
{
   // LTO := Difference between GMT and local time
   struct tm * ptm = gmtime(&whence);

   // for this to work it's mandatory to pass is_dst=-1 to mktime
   ptm->tm_isdst = -1;

   time_t gmt = mktime(ptm);

   return (gmt != -1) ? (whence - gmt) : 0;
}

string GetXmlVpsTimestamp(const TV_SLOT& slot)
{
   int lto = DetermineLto(slot.get_start_t());
   int m, d, y;
   char date_str[50];
   char tz_str[50];

   if (slot.get_vps_date().length() > 0) {
      // VPS data was specified -> reformat only
      if (sscanf(slot.get_vps_date().c_str(), "%2u%2u%2u", &d, &m, &y) != 3)
         assert(false);  // parser failure
      sprintf(date_str, "%04d%02d%02d", y + 2000, m, d);
   }
   else {
      // no VPS date given -> derive from real start time/date
      time_t start_t = slot.get_start_t();
      struct tm * ptm = localtime(&start_t);
      sprintf(date_str, "%04d%02d%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
   }
   // get time zone (format +HHHMM)
   sprintf(tz_str, "00 %+03d%02d", lto / (60*60), abs(lto / 60) % 60);

   return string(date_str) + slot.get_vps_time() + tz_str;
}

/* ------------------------------------------------------------------------------
 * Convert a text string for inclusion in XML as CDATA
 * - the result is not suitable for attributes since quotes are not escaped
 * - input must already have teletext control characters removed
 */
void Latin1ToXml(string& title)
{
   //static const regex expr1("[&<>]");
   //title = regex_replace(title, expr1, T_RESUB_XML());

   // note: & replacement must be first
   title = regex_replace(title, regex("&"), "&amp;");
   title = regex_replace(title, regex("<"), "&lt;");
   title = regex_replace(title, regex(">"), "&gt;");
}

void XmlToLatin1(string& title)
{
   title = regex_replace(title, regex("&gt;"), ">");
   title = regex_replace(title, regex("&lt;"), "<");
   title = regex_replace(title, regex("&amp;"), "&");
}

/* ------------------------------------------------------------------------------
 * Export programme data into XMLTV format
 */
void ExportTitle(FILE * fp, const TV_SLOT& slot, const string& ch_id, TTX_PAGE_DB *db)
{
   assert(slot.get_title().length() > 0);
   assert(slot.get_start_t() != -1);

   {
      string start_str = GetXmlTimestamp(slot.get_start_t());
      string stop_str;
      string vps_str;
      if (slot.get_stop_t() != -1) {
         stop_str = string(" stop=\"") + GetXmlTimestamp(slot.get_stop_t()) + string("\"");
      }
      if (slot.get_vps_time().length() > 0) {
         vps_str = string(" pdc-start=\"") + GetXmlVpsTimestamp(slot) + string("\"");
      }
      string title = slot.get_title();
      // convert all-upper-case titles to lower-case
      // (unless only one consecutive letter, e.g. movie "K2" or "W.I.T.C.H.")
      // TODO move this into separate post-processing stage; make statistics for all titles first
      if (str_all_upper(title)) {
         str_tolower_latin1(title, 1);
      }
      Latin1ToXml(title);

      const TTX_DB_PAGE* pg = db->get_sub_page(slot.get_ov_page_no(), slot.get_ov_page_sub());

      fprintf(fp, "\n<programme start=\"%s\"%s%s channel=\"%s\">\n"
                  "\t<!-- TTX %03X.%04X ACQTS:%ld -->\n"
                  "\t<title>%s</title>\n",
                  start_str.c_str(), stop_str.c_str(), vps_str.c_str(), ch_id.c_str(),
                  slot.get_ov_page_no(), slot.get_ov_page_sub(),
                     ((pg != 0) ? pg->get_timestamp() : 0),
                  title.c_str());

      if (slot.get_subtitle().length() > 0) {
         // repeat the title because often the subtitle is actually just the 2nd part
         // of an overlong title (there's no way to distinguish this from series subtitles etc.)
         // TODO: some channel distinguish title from subtitle by making title all-caps (e.g. TV5)
         string subtitle = slot.get_title() + string(" ") +  slot.get_subtitle();
         Latin1ToXml(subtitle);
         fprintf(fp, "\t<sub-title>%s</sub-title>\n", subtitle.c_str());
      }
      if (slot.get_ttx_ref() != -1) {
         if (slot.get_desc().length() > 0) {
            fprintf(fp, "\t<!-- TTX %03X.%04X -->\n", slot.get_ttx_ref(), slot.get_ttx_ref_sub());
            string desc = slot.get_desc();
            Latin1ToXml(desc);
            fprintf(fp, "\t<desc>%s</desc>\n", desc.c_str());
         }
         else {
            // page not captured or no matching date/title found
            fprintf(fp, "\t<desc>(teletext %03X)</desc>\n", slot.get_ttx_ref());
         }
      }

      // remember to adapt GetXmltvFeat() when adding below
      // video
      const TV_FEAT& feat = slot.get_feat();
      if (   feat.is_video_bw()
          || feat.is_aspect_16_9()
          || feat.is_video_hd()) {
         fprintf(fp, "\t<video>\n");
         if (feat.is_video_bw()) {
            fprintf(fp, "\t\t<colour>no</colour>\n");
         }
         if (feat.is_aspect_16_9()) {
            fprintf(fp, "\t\t<aspect>16:9</aspect>\n");
         }
         if (feat.is_video_hd()) {
            fprintf(fp, "\t\t<quality>HDTV</quality>\n");
         }
         fprintf(fp, "\t</video>\n");
      }
      // audio
      if (feat.is_dolby()) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>surround</stereo>\n\t</audio>\n");
      }
      else if (feat.is_stereo()) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>stereo</stereo>\n\t</audio>\n");
      }
      else if (feat.is_mono()) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>mono</stereo>\n\t</audio>\n");
      }
      else if (feat.is_2chan()) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>bilingual</stereo>\n\t</audio>\n");
      }
      // subtitles
      if (feat.is_omu()) {
         fprintf(fp, "\t<subtitles type=\"onscreen\"/>\n");
      }
      else if (feat.has_subtitles()) {
         fprintf(fp, "\t<subtitles type=\"teletext\"/>\n");
      }
      // tip/highlight (ARD only)
      if (feat.is_tip()) {
         fprintf(fp, "\t<star-rating>\n\t\t<value>1/1</value>\n\t</star-rating>\n");
      }
      fprintf(fp, "</programme>\n");
   }
}


/* ------------------------------------------------------------------------------
 * Parse an XMLTV timestamp (DTD 0.5)
 * - we expect local timezone only (since this is what the ttx grabber generates)
 */
time_t ParseXmltvTimestamp(const char * ts)
{
   int year, mon, mday, hour, min, sec;

   // format "YYYYMMDDhhmmss ZZzzz"
   if (sscanf(ts, "%4d%2d%2d%2d%2d%2d +0000",
              &year, &mon, &mday, &hour, &min, &sec) == 6)
   {
      struct tm tm_obj = {};  // zero-initialized

      tm_obj.tm_min = min;
      tm_obj.tm_hour = hour;
      tm_obj.tm_mday = mday;
      tm_obj.tm_mon = mon - 1;
      tm_obj.tm_year = year - 1900;
      tm_obj.tm_isdst = -1;

      /*
       * Hack to get mktime() to take an operand in GMT instead of localtime:
       * Calculate the offset from GMT to adjust the argument given to mktime().
       */
#if defined(_BSD_SOURCE)
      time_t ts = timegm( &tm_obj );
#else
      time_t ts = mktime( &tm_obj );
      //ts += tm_obj.tm_gmtoff;  // glibc extension (_BSD_SOURCE)
      ts += 60*60 * tm_obj.tm_isdst - timezone;
#endif

      return ts;
   }
   else {
      return -1;
   }
}

string GetXmltvTitle(const string& xml)
{
   smatch whats;
   string title;

   static const regex expr1("<title>([\\s\\S]*)</title>");
   if (regex_search(xml, whats, expr1)) {
      title.assign(whats[1]);
      XmlToLatin1(title);
   }
   return title;
}

string GetXmltvSubTitle(const string& xml)
{
   smatch whats;
   string subtitle;

   static const regex expr1("<sub-title>([\\s\\S]*)</sub-title>");
   if (regex_search(xml, whats, expr1)) {
      subtitle.assign(whats[1]);
      XmlToLatin1(subtitle);
   }
   return subtitle;
}

string GetXmltvDescription(const string& xml)
{
   smatch whats;
   string desc;

   // Note "[\\s\\S]" is used for matching any char (i.e. including "\n", which "." would not allow)
   static const regex expr1("<desc>([\\s\\S]*)</desc>");
   if (regex_search(xml, whats, expr1)) {
      desc.assign(whats[1]);
      XmlToLatin1(desc);
   }
   return desc;
}

time_t GetXmltvStopTime(const string& xml, time_t old_ts)
{
   smatch whats;
   static const regex expr1("stop=\"([^\"]*)\"");

   if (regex_search(xml, whats, expr1)) {
      return ParseXmltvTimestamp(&whats[1].first[0]);
   }
   else {
      return old_ts;
   }
}

// This is the inverse of feature description in ExportTitle()
TV_FEAT GetXmltvFeat(const string& xml)
{
   TV_FEAT feat;
   smatch whats;

   static const regex expr01("<video>[\\s\\S]*<colour>no</colour>[\\s\\S]*</video>");
   if (regex_search(xml, whats, expr01))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_BW);

   static const regex expr02("<video>[\\s\\S]*<aspect>16:9</aspect>[\\s\\S]*</video>");
   if (regex_search(xml, whats, expr02))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_ASPECT_16_9);

   static const regex expr03("<video>[\\s\\S]*<quality>HDTV</quality>[\\s\\S]*</video>");
   if (regex_search(xml, whats, expr03))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_HD);

   static const regex expr04("<audio>[\\s\\S]*<stereo>surround</stereo>[\\s\\S]*</audio>");
   if (regex_search(xml, whats, expr04))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_DOLBY);

   static const regex expr05("<audio>[\\s\\S]*<stereo>stereo</stereo>[\\s\\S]*</audio>");
   if (regex_search(xml, whats, expr05))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_STEREO);

   static const regex expr06("<audio>[\\s\\S]*<stereo>mono</stereo>[\\s\\S]*</audio>");
   if (regex_search(xml, whats, expr06))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_MONO);

   static const regex expr07("<audio>[\\s\\S]*<stereo>bilingual</stereo>[\\s\\S]*</audio>");
   if (regex_search(xml, whats, expr07))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_2CHAN);

   static const regex expr08("<subtitles type=\"onscreen\"/>");
   if (regex_search(xml, whats, expr08))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_OMU);

   static const regex expr09("<subtitles type=\"teletext\"/>");
   if (regex_search(xml, whats, expr09))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_SUBTITLES);

   static const regex expr10("<star-rating>\\s*<value>\\s*1/1\\s*</value>\\s*</star-rating>");
   if (regex_search(xml, whats, expr10))
      feat.m_flags |= (1 << TV_FEAT::TV_FEAT_TIP);

   return feat;
}

/* ------------------------------------------------------------------------------
 * Read an XMLTV input file
 * - note this is NOT a full XML parser (not by far)
 *   will only work with XMLTV files generated by this grabber
 */
void XMLTV::ImportXmltvFile(const char * fname)
{
   map<string,int> ChnProgDef;
   enum T_IMPORT_STATE {
      STATE_NULL,
      STATE_DONE,
      STATE_TV,
      STATE_CHN,
      STATE_PROG
   } state = STATE_NULL;
   string tag_data;
   string chn_id;
   time_t start_t = -1;
   time_t exp_thresh = time(NULL) - m_expire_min * 60;
   cmatch what;

   FILE * fp = fopen(fname, "r");
   if (fp != NULL)
   {
      char buf[1024];
      while (fgets(buf, sizeof(buf), fp) != 0) {
         // handling XML header
         if (state == STATE_NULL) {
            static const regex expr1("^\\s*<(\\?xml|!)[^>]*>\\s*$", regex::icase);
            static const regex expr2("^\\s*<tv([^>\"=]+(=\"[^\"]*\")?)*>\\s*$", regex::icase);
            static const regex expr3("\\S");

            if (regex_search(buf, what, expr1)) {
               // swallow DTD header
            }
            else if (regex_search(buf, what, expr2)) {
               // <tv> top-level tag found
               state = STATE_TV;
            }
            else if (regex_search(buf, what, expr3)) {
               fprintf(stderr, "Unexpected line '%s' in %s\n", buf, fname);
            }
         }
         else if (state == STATE_DONE) {
            fprintf(stderr, "Unexpected line '%s' following </tv> in %s\n", buf, fname);
         }
         // handling main section in-between <tv> </tv>
         else if (state == STATE_TV) {
            static const regex expr1("^\\s*</tv>\\s*$", regex::icase);
            static const regex expr2("^\\s*<channel", regex::icase);
            static const regex expr3("^\\s*<programme", regex::icase);
            static const regex expr4("\\S");
            if (regex_search(buf, what, expr1)) {
               state = STATE_DONE;
            }
            else if (regex_search(buf, what, expr2)) {
               static const regex expr5("id=\"([^\"]*)\"", regex::icase);
               if (regex_search(buf, what, expr5)) {
                  chn_id.assign(what[1]);
               } else {
                  fprintf(stderr, "Missing 'id' attribute in '%s' in %s\n", buf, fname);
               }
               tag_data = buf;
               state = STATE_CHN;
            }
            else if (regex_search(buf, what, expr3)) {
               static const regex expr6("start=\"([^\"]*)\"[\\s\\S]*channel=\"([^\"]*)\"", regex::icase);
               if (regex_search(buf, what, expr6)) {
                  chn_id.assign(what[2]);
                  start_t = ParseXmltvTimestamp(&what[1].first[0]);
                  if (m_merge_chn.find(chn_id) == m_merge_chn.end()) {
                     fprintf(stderr, "Unknown channel %s in merge input\n", chn_id.c_str());
                     exit(2);
                  }
               } else {
                  fprintf(stderr, "Warning: Missing 'start' and 'channel' attributes "
                          "in '%s' in %s\n", buf, fname);
                  chn_id.clear();
               }
               tag_data = buf;
               state = STATE_PROG;
            }
            else if (regex_search(buf, what, expr4)) {
               fprintf(stderr, "Warning: Unexpected tag '%s' in %s ignored "
                       "(expecting <channel> or <programme>)\n", buf, fname);
            }
         }
         // handling channel data
         else if (state == STATE_CHN) {
            tag_data += buf;
            static const regex expr7("^\\s*</channel>\\s*$", regex::icase);
            if (regex_search(buf, what, expr7)) {
               if (opt_debug) printf("XMLTV import channel '%s'\n", chn_id.c_str());
               m_merge_chn[chn_id] = tag_data;
               state = STATE_TV;
            }
         }
         // handling programme data
         else if (state == STATE_PROG) {
            tag_data += buf;
            static const regex expr8("^\\s*</programme>\\s*$", regex::icase);
            if (regex_search(buf, what, expr8)) {
               if (   ((start_t >= exp_thresh) || (m_expire_min < 0))
                   && (chn_id.length() > 0)) {
                  char buf[20];
                  sprintf(buf, "%ld;", (long)start_t);
                  m_merge_prog[string(buf) + chn_id] = tag_data;
                  if (opt_debug) printf("XMLTV import programme '%s' start:%ld %s\n",
                                        GetXmltvTitle(tag_data).c_str(), (long)start_t, chn_id.c_str());
                  // remember that there's at least one programme for this channel
                  ChnProgDef[chn_id] = 1;
               }
               state = STATE_TV;
            }
         }
      }
      fclose(fp);

      // remove empty channels
      for (map<string,string>::iterator p = m_merge_chn.begin(); p != m_merge_chn.end(); ) {
         if (ChnProgDef.find(p->first) == ChnProgDef.end()) {
            if (opt_debug) printf("XMLTV input: dropping empty CHANNEL ID %s\n", p->first.c_str());
            m_merge_chn.erase(p++);
         } else {
            p++;
         }
      }
   }
   else {
      fprintf(stderr, "cannot open merge input file '%s': %s\n", fname, strerror(errno));
   }
}

/* ------------------------------------------------------------------------------
 * Merge description and other meta-data from old source, if missing in new source
 * - called before programmes in the old source are discarded
 */
void MergeSlotDesc(TV_SLOT& slot, const string& old_xml,
                   time_t new_start, time_t new_stop,
                   time_t old_start, time_t old_stop)
{
   if (   (old_start == new_start)
       && (   (old_stop == new_stop)
           || (old_stop == old_start) || (new_stop == new_start)))
   {
      string old_title = GetXmltvTitle(old_xml);
      const string& new_title = slot.get_title();
      unsigned dpos_new;
      unsigned dpos_old;

      // FIXME allow for parity errors (& use redundancy to correct them)
      if (   str_cmp_alnum(new_title, old_title, &dpos_new, &dpos_old)
          || (dpos_new >= new_title.size()) )
      {
         if (new_title.size() < old_title.size())
         {
            string old_subtitle = GetXmltvSubTitle(old_xml);
            // undo concatenation, i.e. remove title text from the front of the sub-title
            str_cmp_alnum(old_title, old_subtitle, &dpos_new, &dpos_old);
            old_subtitle.erase(old_subtitle.begin(), old_subtitle.begin() + dpos_old);

            slot.merge_title(old_title, old_subtitle);
         }

         string old_desc = GetXmltvDescription(old_xml);
         const string& new_desc = slot.get_desc();

         // select the longer description text among new and old versions
         // ignore empty descriptions with only a TTX page reference
         static const regex expr2("^\\(teletext [1-8][0-9][0-9]\\)$");
         smatch whats;
         if (   (old_desc.length() > 0)
             && !regex_search(old_desc, whats, expr2)
             && (   (old_desc.length() > new_desc.length())
                 || regex_search(new_desc, whats, expr2) ))
         {
            // copy old description
            slot.merge_desc(old_desc);

            // also copy older feature flags (some may come from description page)
            slot.merge_feat(GetXmltvFeat(old_xml));
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Merge old and new programmes
 */
int MergeNextSlot( list<TV_SLOT>& NewSlotList,
                   list<time_t>& OldSlotList,
                   const map<time_t,string>& OldProgHash )
{
   const string * p_xml = 0;
   time_t new_start = -1;
   time_t new_stop = -1;
   time_t old_start = -1;
   time_t old_stop = -1;

   // get the first (oldest) programme start/stop times from both sources
   if (!NewSlotList.empty()) {
      TV_SLOT& slot = NewSlotList.front();
      new_start = slot.get_start_t();
      new_stop = slot.get_stop_t();
      if (new_stop == -1)  // FIXME
         new_stop = new_start + 1;

      // remove overlapping (or repeated) programmes in the new data
      list<TV_SLOT>::iterator it_next_slot = NewSlotList.begin();
      ++it_next_slot;
      while (   (it_next_slot != NewSlotList.end())
             && ((*it_next_slot).get_start_t() < new_stop)) {
         if (opt_debug) printf("MERGE DISCARD NEW %ld '%.30s' ovl %ld..%ld\n",
                               (*it_next_slot).get_start_t(), (*it_next_slot).get_title().c_str(),
                               (long)new_start, (long)new_stop);
         NewSlotList.erase(it_next_slot++);
      }
   }
   if (!OldSlotList.empty()) {
      p_xml = &OldProgHash.find(OldSlotList.front())->second;
      old_start = OldSlotList.front();
      old_stop = GetXmltvStopTime(*p_xml, old_start);
   }

   if ((new_start != -1) && (old_start != -1)) {
      TV_SLOT& slot = NewSlotList.front();
      if (opt_debug) printf("MERGE CMP %s -- %.30s\n", slot.get_title().c_str(), GetXmltvTitle(*p_xml).c_str());
      // discard old programmes which overlap the next new one
      // TODO: merge description from old to new if time and title are identical and new has no description
      while ((old_start < new_stop) && (old_stop > new_start)) {
         if (opt_debug) printf("MERGE DISCARD OLD %ld...%ld  ovl %ld..%ld\n",
                               (long)old_start, (long)old_stop, (long)new_start, (long)new_stop);
         MergeSlotDesc(slot, *p_xml, new_start, new_stop, old_start, old_stop);
         OldSlotList.pop_front();

         if (!OldSlotList.empty()) {
            p_xml = &OldProgHash.find(OldSlotList.front())->second;
            old_start = OldSlotList.front();
            old_stop = GetXmltvStopTime(*p_xml, old_start);
            if (opt_debug) printf("MERGE CMP %s -- %.30s\n",
                                  slot.get_title().c_str(), GetXmltvTitle(*p_xml).c_str());
         } else {
            old_start = -1;
            old_stop = -1;
            break;
         }
      }
      if ((old_start == -1) || (new_start <= old_start)) {
         // new programme starts earlier -> choose data from new source
         // discard old programmes which overlap the next new one
         while (!OldSlotList.empty() && (OldSlotList.front() < new_stop)) {
            if (opt_debug) printf("MERGE DISCARD2 OLD %ld ovl STOP %ld\n",
                                  (long)OldSlotList.front(), (long)new_stop);
            MergeSlotDesc(slot, *p_xml, new_start, new_stop, old_start, old_stop);
            OldSlotList.pop_front();
         }
         if (opt_debug) printf("MERGE CHOOSE NEW %ld..%ld\n", new_start, new_stop);
         return 1; // new
      } else {
         // choose data from old source
         if (opt_debug) printf("MERGE CHOOSE OLD %ld..%ld\n", old_start, old_stop);
         return 2; // old
      }
   }
   // special cases: only one source available anymore
   else if (new_start != -1) {
      return 1; // new
   }
   else if (old_start != -1) {
      return 2; // old
   }
   else {
      return 0; // error
   }
}

/* ------------------------------------------------------------------------------
 * Write grabbed data to XMLTV
 * - merge with old data, if available
 */
void XMLTV::ExportXmltv(list<TV_SLOT>& NewSlots, const char * p_file_name,
                        const char * p_my_ver, const char * p_my_url)
{
   FILE * fp;

   if (p_file_name != 0) {
      fp = fopen(p_file_name, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", p_file_name, strerror(errno));
         exit(1);
      }
   }
   else {
      fp = stdout;
   }

   fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
               "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n");
   if (!p_my_ver || !p_my_url) {
      fprintf(fp, "<tv>\n");
   }
   else {
      fprintf(fp, "<tv generator-info-name=\"%s\" generator-info-url=\"%s\" "
                  "source-info-name=\"teletext\">\n",
                  p_my_ver, p_my_url);
   }

   // print channel table
   //map<string,string>::iterator p_chn = m_merge_chn.find(m_ch_id);
   if (m_merge_chn.find(m_ch_id) == m_merge_chn.end()) {
      fprintf(fp, "<channel id=\"%s\">\n"
                  "\t<display-name>%s</display-name>\n"
                  "</channel>\n",
                  m_ch_id.c_str(), m_ch_name.c_str());
   }
   for (map<string,string>::iterator p = m_merge_chn.begin(); p != m_merge_chn.end(); p++) {
      if (p->first == m_ch_id) {
         fprintf(fp, "<channel id=\"%s\">\n"
                     "\t<display-name>%s</display-name>\n"
                     "</channel>\n", m_ch_id.c_str(), m_ch_name.c_str());
      }
      else {
         fprintf(fp, "%s", p->second.c_str());
      }
   }

   if (m_merge_chn.find(m_ch_id) != m_merge_chn.end()) {
      // extract respective channel's data from merge input
      map<time_t,string> OldProgHash;
      for (map<string,string>::iterator p = m_merge_prog.begin(); p != m_merge_prog.end(); ) {
         long start_ts;
         int slen;
         if (   (sscanf(p->first.c_str(), "%ld;%n", &start_ts, &slen) >= 1)
             && (p->first.compare(slen, p->first.length() - slen, m_ch_id) == 0) ) {
            OldProgHash[start_ts] = p->second;
            m_merge_prog.erase(p++);
         }
         else {
            p++;
         }
      }

      // map holding old programmes is already sorted, as start time is used as key
      // (also, new list is expected to be sorted by caller)
      list<time_t> OldSlotList;
      for (map<time_t,string>::iterator p = OldProgHash.begin(); p != OldProgHash.end(); p++)
         OldSlotList.push_back(p->first);

      // combine both sources (i.e. merge them)
      while (!NewSlots.empty() || !OldSlotList.empty()) {
         switch (MergeNextSlot(NewSlots, OldSlotList, OldProgHash)) {
            case 1:
               assert(!NewSlots.empty());
               ExportTitle(fp, NewSlots.front(), m_ch_id, &mp_db->page_db);
               NewSlots.pop_front();
               break;
            case 2:
               assert(!OldSlotList.empty());
               fprintf(fp, "\n%s", OldProgHash[OldSlotList.front()].c_str());
               OldSlotList.pop_front();
               break;
            default:
               break;
         }
      }
   }
   else {
      // no merge required -> simply print all new
      //for (list<TV_SLOT>::iterator p = NewSlots.begin(); p != NewSlots.end(); p++)
      //   ExportTitle(fp, *(*p), m_ch_id);
      while (!NewSlots.empty()) {
         ExportTitle(fp, NewSlots.front(), m_ch_id, &mp_db->page_db);

         NewSlots.pop_front();
      }
   }

   // append data for all remaining old channels unchanged
   for (map<string,string>::iterator p = m_merge_prog.begin(); p != m_merge_prog.end(); p++) {
      fprintf(fp, "\n%s", p->second.c_str());
   }

   fprintf(fp, "</tv>\n");
   fclose(fp);
}

/* ------------------------------------------------------------------------------
 * Assign channel names for use during export
 */
void XMLTV::SetChannelName(const char * user_chname, const char * user_chid)
{
   // get channel name from teletext header packets
   m_ch_name = user_chname ? string(user_chname) : ParseChannelName(&mp_db->page_db);

   m_ch_id = user_chid ? string(user_chid) : mp_db->chn_id.get_ch_id();

   if (m_ch_name.length() == 0) {
      m_ch_name = m_ch_id;
   }
   if (m_ch_name.length() == 0) {
      m_ch_name = "???";
   }
   if (m_ch_id.length() == 0) {
      m_ch_id = m_ch_name;
   }
}

/* ------------------------------------------------------------------------------
 * Optionally assign expire threshold for use during import
 * - given value must be in unit of minutes
 */
void XMLTV::SetExpireTime(int expire_min)
{
   // must be set before import
   m_expire_min = expire_min;
}

