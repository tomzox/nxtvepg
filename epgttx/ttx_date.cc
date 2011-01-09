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
 * $Id: ttx_date.cc,v 1.2 2011/01/06 16:58:44 tom Exp tom $
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


/* ------------------------------------------------------------------------------
 * Tables to support parsing of dates in various languages
 * - the hash tables map each word to an array with 3 elements:
 *   0: bitfield of language (language indices as in teletext header)
 *      use a bitfield because some words are identical in different languages
 *   1: value (i.e. month or weekday index)
 *   2: bitfield abbreviation: 1:non-abbr., 2:abbr., 3:no change by abbr.
 *      use a bitfield because some short words are not abbreviated (e.g. "may")
 */

enum DATE_NAME_TYPE
{
  DATE_NAME_NONE = 0,
  DATE_NAME_FULL = 1,
  DATE_NAME_ABBRV = 2,
  DATE_NAME_ANY = 3
};
struct T_DATE_NAME
{
  const char  * p_name;
  int           lang_bits;
  int           idx;
  DATE_NAME_TYPE abbrv;
};

// map month names to month indices 1..12
const T_DATE_NAME MonthNames[] =
{
   // English (minus april, august, september)
   {"january", (1<<0),1, DATE_NAME_FULL},
   {"february", (1<<0),2, DATE_NAME_FULL},
   {"march", (1<<0),3, DATE_NAME_FULL},
   {"may", (1<<0),5, DATE_NAME_ANY},  // abbreviation == full name
   {"june", (1<<0),6, DATE_NAME_FULL},
   {"july", (1<<0),7, DATE_NAME_FULL},
   {"october", (1<<0),10, DATE_NAME_FULL},
   {"december", (1<<0),12, DATE_NAME_FULL},
   {"mar", (1<<0),3, DATE_NAME_ABBRV},
   {"oct", (1<<0),10, DATE_NAME_ABBRV},
   {"dec", (1<<0),12, DATE_NAME_FULL},
   // German
   {"januar", (1<<1),1, DATE_NAME_FULL},
   {"februar", (1<<1),2, DATE_NAME_FULL},
   {"märz", (1<<1),3, DATE_NAME_FULL},
   {"april", (1<<1)|(1<<0),4, DATE_NAME_FULL},
   {"mai", (1<<4)|(1<<1),5, DATE_NAME_ANY},  // abbreviation == full name
   {"juni", (1<<1),6, DATE_NAME_FULL},
   {"juli", (1<<1),7, DATE_NAME_FULL},
   {"august", (1<<1)|(1<<0),8, DATE_NAME_FULL},
   {"september", (1<<1)|(1<<0),9, DATE_NAME_FULL},
   {"oktober", (1<<1),10, DATE_NAME_FULL},
   {"november", (1<<1)|(1<<0),11, DATE_NAME_FULL},
   {"dezember", (1<<1),12, DATE_NAME_FULL},
   {"jan", (1<<1)|(1<<0),1, DATE_NAME_ABBRV},
   {"feb", (1<<1)|(1<<0),2, DATE_NAME_ABBRV},
   {"mär", (1<<1),3, DATE_NAME_ABBRV},
   {"mar", (1<<1),3, DATE_NAME_ABBRV},
   {"apr", (1<<1)|(1<<0),4, DATE_NAME_ABBRV},
   {"jun", (1<<1)|(1<<0),6, DATE_NAME_ABBRV},
   {"jul", (1<<1)|(1<<0),7, DATE_NAME_ABBRV},
   {"aug", (1<<1)|(1<<0),8, DATE_NAME_ABBRV},
   {"sep", (1<<1)|(1<<0),9, DATE_NAME_ABBRV},
   {"okt", (1<<1),10, DATE_NAME_ABBRV},
   {"nov", (1<<1)|(1<<0),11, DATE_NAME_ABBRV},
   {"dez", (1<<1),12, DATE_NAME_ABBRV},
   // French
   {"janvier", (1<<4),1, DATE_NAME_FULL},
   {"février", (1<<4),2, DATE_NAME_FULL},
   {"mars", (1<<4),3, DATE_NAME_FULL},
   {"avril", (1<<4),4, DATE_NAME_FULL},
   {"juin", (1<<4),6, DATE_NAME_FULL},
   {"juillet", (1<<4),7, DATE_NAME_FULL},
   {"août", (1<<4),8, DATE_NAME_FULL},
   {"septembre", (1<<4),9, DATE_NAME_FULL},
   {"octobre", (1<<4),10, DATE_NAME_FULL},
   {"novembre", (1<<4),11, DATE_NAME_FULL},
   {"décembre", (1<<4),12, DATE_NAME_FULL},
   {NULL, 0, 0, DATE_NAME_NONE}
};

// map week day names to indices 0..6 (0=sunday)
const T_DATE_NAME WDayNames[] =
{
   // English
   {"sat", (1<<0),6, DATE_NAME_ABBRV},
   {"sun", (1<<0),0, DATE_NAME_ABBRV},
   {"mon", (1<<0),1, DATE_NAME_ABBRV},
   {"tue", (1<<0),2, DATE_NAME_ABBRV},
   {"wed", (1<<0),3, DATE_NAME_ABBRV},
   {"thu", (1<<0),4, DATE_NAME_ABBRV},
   {"fri", (1<<0),5, DATE_NAME_ABBRV},
   {"saturday", (1<<0),6, DATE_NAME_FULL},
   {"sunday", (1<<0),0, DATE_NAME_FULL},
   {"monday", (1<<0),1, DATE_NAME_FULL},
   {"tuesday", (1<<0),2, DATE_NAME_FULL},
   {"wednesday", (1<<0),3, DATE_NAME_FULL},
   {"thursday", (1<<0),4, DATE_NAME_FULL},
   {"friday", (1<<0),5, DATE_NAME_FULL},
   // German
   {"sa", (1<<1),6, DATE_NAME_ABBRV},
   {"so", (1<<1),0, DATE_NAME_ABBRV},
   {"mo", (1<<1),1, DATE_NAME_ABBRV},
   {"di", (1<<1),2, DATE_NAME_ABBRV},
   {"mi", (1<<1),3, DATE_NAME_ABBRV},
   {"do", (1<<1),4, DATE_NAME_ABBRV},
   {"fr", (1<<1),5, DATE_NAME_ABBRV},
   {"sam", (1<<1),6, DATE_NAME_ABBRV},
   {"son", (1<<1),0, DATE_NAME_ABBRV},
   {"mon", (1<<1),1, DATE_NAME_ABBRV},
   {"die", (1<<1),2, DATE_NAME_ABBRV},
   {"mit", (1<<1),3, DATE_NAME_ABBRV},
   {"don", (1<<1),4, DATE_NAME_ABBRV},
   {"fre", (1<<1),5, DATE_NAME_ABBRV},
   {"samstag", (1<<1),6, DATE_NAME_FULL},
   {"sonnabend", (1<<1),6, DATE_NAME_FULL},
   {"sonntag", (1<<1),0, DATE_NAME_FULL},
   {"montag", (1<<1),1, DATE_NAME_FULL},
   {"dienstag", (1<<1),2, DATE_NAME_FULL},
   {"mittwoch", (1<<1),3, DATE_NAME_FULL},
   {"donnerstag", (1<<1),4, DATE_NAME_FULL},
   {"freitag", (1<<1),5, DATE_NAME_FULL},
   // French
   {"samedi", (1<<4),6, DATE_NAME_FULL},
   {"dimanche", (1<<4),0, DATE_NAME_FULL},
   {"lundi", (1<<4),1, DATE_NAME_FULL},
   {"mardi", (1<<4),2, DATE_NAME_FULL},
   {"mercredi", (1<<4),3, DATE_NAME_FULL},
   {"jeudi", (1<<4),4, DATE_NAME_FULL},
   {"vendredi", (1<<4),5, DATE_NAME_FULL},
   {NULL}
};

// map today, tomorrow etc. to day offsets 0,1,...
const T_DATE_NAME RelDateNames[] =
{
   {"today", (1<<0),0, DATE_NAME_FULL},
   {"tomorrow", (1<<0),1, DATE_NAME_FULL},
   {"heute", (1<<1),0, DATE_NAME_FULL},
   {"morgen", (1<<1),1, DATE_NAME_FULL},
   {"übermorgen", (1<<1),2, DATE_NAME_FULL},
   {"Übermorgen", (1<<1),2, DATE_NAME_FULL},  // hack to work around mismatching locale
   {"aujourd'hui", (1<<4),0, DATE_NAME_FULL},
   {"demain", (1<<4),1, DATE_NAME_FULL},
   {"aprés-demain", (1<<4),2, DATE_NAME_FULL},
   {NULL}
};

// return a reg.exp. pattern which matches all names of a given language
string GetDateNameRegExp(const T_DATE_NAME * p_list, int lang, int abbrv)
{
   string pat;
   for (const T_DATE_NAME * p = p_list; p->p_name != NULL; p++) {
      if ((p->lang_bits & (1 << lang)) != 0) {
         if ((p->abbrv & abbrv) != 0) {
            if (pat.length() > 0)
               pat += "|";
            pat += p->p_name;
         }
      }
   }
   return pat;
}

const T_DATE_NAME * MapDateName(const char * p_name, const T_DATE_NAME * p_list)
{
   for (const T_DATE_NAME * p = p_list; p->p_name != NULL; p++) {
      if (strcasecmp(p->p_name, p_name) == 0)
         return p;
   }
   return 0;
}

int GetWeekDayOffset(string wday_name, time_t timestamp)
{
   int reldate = -1;

   if (wday_name.length() > 0) {
      const T_DATE_NAME * p = MapDateName(wday_name.c_str(), WDayNames);
      if (p != 0) {
         int wday_idx = p->idx;
         struct tm * ptm = localtime(&timestamp);
         int cur_wday_idx = ptm->tm_wday;

         if (wday_idx >= cur_wday_idx) {
            reldate = wday_idx - cur_wday_idx;
         }
         else {
            reldate = (7 - cur_wday_idx) + wday_idx;
         }
      }
   }
   return reldate;
}

bool CheckDate(int mday, int month, int year,
               string wday_name, string month_name,
               time_t timestamp,
               int * ret_mday, int * ret_mon, int * ret_year)
{
   // first check all provided values
   if (   ((mday != -1) && !((mday <= 31) && (mday > 0)))
       || ((month != -1) && !((month <= 12) && (month > 0)))
       || ((year != -1) && !((year < 100) || ((year >= 2006) && (year < 2031)))) )
   {
      return false;
   }

   if ((wday_name.length() > 0) && (mday == -1))
   {
      // determine date from weekday name alone
      int reldate = GetWeekDayOffset(wday_name, timestamp);
      if (reldate < 0)
         return false;

      time_t whence = timestamp + reldate*24*60*60;
      struct tm * ptm = localtime(&whence);
      mday = ptm->tm_mday;
      month = ptm->tm_mon + 1;
      year = ptm->tm_year + 1900;
      if (opt_debug) printf("DESC DATE %s %d.%d.%d\n", wday_name.c_str(), mday, month, year);
   }
   else if (mday != -1)
   {
      if (wday_name.length() > 0) {
         if (!MapDateName(wday_name.c_str(), WDayNames))
            return false;
      }
      if ((month == -1) && (month_name.length() > 0)) {
         const T_DATE_NAME * p = MapDateName(month_name.c_str(), MonthNames);
         if (p == 0)
            return false;
         month = p->idx;
      }
      else if (month == -1) {
         return false;
      }
      if (year == -1) {
         struct tm * ptm = localtime(&timestamp);
         year = ptm->tm_year + 1900;
      }
      else if (year < 100) {
         struct tm * ptm = localtime(&timestamp);
         int cur_year = ptm->tm_year + 1900;
         year += (cur_year - cur_year % 100);
      }
      if (opt_debug) printf("DESC DATE %d.%d.%d\n", mday, month, year);
   }
   else {
      return false;
   }
   *ret_mday = mday;
   *ret_mon = month;
   *ret_year = year;
   return true;
}

/* ------------------------------------------------------------------------------
 * Parse date in an overview page
 * - similar to dates on description pages
 */
bool T_PG_DATE::ParseOvDate(int page, int sub, int head)
{
   int reldate = -1;
   int mday = -1;
   int month = -1;
   int year = -1;

   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   int lang = pgtext->get_lang();

   string wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
   string wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
   string mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
   string relday_match = GetDateNameRegExp(RelDateNames, lang, DATE_NAME_ANY);
   smatch whats;
   int prio = -1;

   for (int line = 1; line < head; line++)
   {
      const string& text = pgtext->get_text(line);

      // [Mo.]13.04.[2006]
      // So,06.01.
      static regex expr1[8];
      static regex expr2[8];
      if (expr1[lang].empty()) {
         expr1[lang].assign(string("(^| |(") + wday_abbrv_match + ")(\\.|\\.,|,) ?)(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2}|\\d{4})?([ ,:]|$)", regex::icase);
         expr2[lang].assign(string("(^| |(") + wday_match + ")(, ?| ))(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2}|\\d{4})?([ ,:]|$)", regex::icase);
      }
      if (   regex_search(text, whats, expr1[lang])
          || regex_search(text, whats, expr2[lang]) )
      {
         if (CheckDate(atoi_substr(whats[4]), atoi_substr(whats[5]), atoi_substr(whats[6]),
                       "", "", pgtext->get_timestamp(), &mday, &month, &year)) {
            prio = 3;
         }
      }
      // 13.April [2006]
      static regex expr3[8];
      if (expr3[lang].empty()) {
         expr3[lang].assign(string("(^| )(\\d{1,2})\\. ?(") + mname_match + ")( (\\d{2}|\\d{4}))?([ ,:]|$)", regex::icase);
      }
      if (regex_search(text, whats, expr3[lang])) {
         if (CheckDate(atoi_substr(whats[2]), -1, atoi_substr(whats[5]),
                       "", string(whats[3]), pgtext->get_timestamp(),
                       &mday, &month, &year)) {
            prio = 3;
         }
      }
      // Sunday 22 April (i.e. no dot after month)
      static regex expr4[8];
      static regex expr5[8];
      if (expr4[lang].empty()) {
         expr4[lang].assign(string("(^| )(") + wday_match + string(")(, ?| )(\\d{1,2})\\.? ?(") + mname_match + ")( (\\d{4}|\\d{2}))?( |,|;|$)", regex::icase);
         expr5[lang].assign(string("(^| )(") + wday_abbrv_match + string(")(\\.,? ?|, ?| )(\\d{1,2})\\.? ?(") + mname_match + ")( (\\d{4}|\\d{2}))?( |,|;|$)", regex::icase);
      }
      if (   regex_search(text, whats, expr4[lang])
          || regex_search(text, whats, expr5[lang])) {
         if (CheckDate(atoi_substr(whats[4]), -1, atoi_substr(whats[7]),
                       string(whats[2]), string(whats[5]), pgtext->get_timestamp(),
                       &mday, &month, &year)) {
            prio = 3;
         }
      }

      if (prio >= 3) continue;

      // "Do. 21-22 Uhr" (e.g. on VIVA)  --  TODO internationalize "Uhr"
      // "Do  21:00-22:00" (e.g. Tele-5)
      static regex expr6[8];
      if (expr6[lang].empty()) {
         expr6[lang].assign(string("(^| )((") + wday_abbrv_match + string(")\\.?|(") + wday_match + ")) *((\\d{1,2}(-| - )\\d{1,2}( +Uhr| ?h|   ))|(\\d{1,2}[\\.:]\\d{2}(-| - )(\\d{1,2}[\\.:]\\d{2})?))( |$)", regex::icase);
      }
      if (regex_search(text, whats, expr6[lang])) {
         string wday_name;
         if (whats[2].matched)
            wday_name.assign(whats[2]);
         else
            wday_name.assign(whats[3]);
         int off = GetWeekDayOffset(wday_name, pgtext->get_timestamp());
         if (off >= 0)
            reldate = off;
         prio = 2;
      }

      if (prio >= 2) continue;

      // monday, tuesday, ... (non-abbreviated only)
      static regex expr7[8];
      if (expr7[lang].empty()) {
         expr7[lang].assign(string("(^| )(") + wday_match + ")([ ,:]|$)", regex::icase);
      }
      if (regex_search(text, whats, expr7[lang])) {
         int off = GetWeekDayOffset(string(whats[2]), pgtext->get_timestamp());
         if (off >= 0)
            reldate = off;
         prio = 1;
      }

      if (prio >= 1) continue;

      // today, tomorrow, ...
      static regex expr8[8];
      if (expr8[lang].empty()) {
         expr8[lang].assign(string("(^| )(") + relday_match + ")([ ,:]|$)", regex::icase);
      }
      if (regex_search(text, whats, expr8[lang])) {
         string rel_name = string(whats[2]);
         const T_DATE_NAME * p = MapDateName(rel_name.c_str(), RelDateNames);
         if (p != 0)
            reldate = p->idx;
         prio = 0;
      }
   }

   if ((mday == -1) && (reldate != -1)) {
      time_t whence = pgtext->get_timestamp() + reldate*24*60*60;
      struct tm * ptm = localtime(&whence);
      mday = ptm->tm_mday;
      month = ptm->tm_mon + 1;
      year = ptm->tm_year + 1900;
   }
   else if ((year != -1) && (year < 100)) {
      time_t whence = pgtext->get_timestamp();
      struct tm * ptm = localtime(&whence);
      int cur_year = ptm->tm_year + 1900;
      year += (cur_year - cur_year % 100);
   }
   else if (year == -1) {
      if (opt_debug) printf("OV DATE %03X.%04X: no match\n", page, sub);
   }

   m_mday = mday;
   m_month = month;
   m_year = year;
   m_date_off = 0;

   return (year != -1);
}

/* ------------------------------------------------------------------------------
 * Parse description pages
 *

 *ARD:
 *                          Mi 12.04.06 Das Erste 
 *                                                
 *         11.15 - 12.00 Uhr                      
 *         In aller Freundschaft           16:9/UT
 *         Ehen auf dem Prüfstand                 

 *ZDF:
 *  ZDFtext          ZDF.aspekte
 *  Programm  Freitag, 21.April   [no time!]

 *MDR:
 *Mittwoch, 12.04.2006, 14.30-15.30 Uhr  
 *                                        
 * LexiTV - Wissen für alle               
 * H - wie Hühner                        

 *3sat:
 *      Programm         Heute            
 *      Mittwoch, 12. April               
 *                         1D107 120406 3B
 *  07.00 - 07.30 Uhr            VPS 0700 
 *  nano Wh.                              

 * Date format:
 * ARD:   Fr 14.04.06 // 15.40 - 19.15 Uhr
 * ZDF:   Freitag, 14.April // 15.35 - 19.00 Uhr
 * Sat.1: Freitag 14.04. // 16:00-17:00
 * RTL:   Fr[.] 14.04. 13:30 Uhr
 * Pro7:  So 16.04 06:32-07:54 (note: date not on 2/2, only title rep.)
 * RTL2:  Freitag, 14.04.06; 12.05 Uhr (at bottom) (1/6+2/6 for movie)
 * VOX:   Fr 18:00 Uhr
 * MTV:   Fr 14.04 18:00-18:30 // Sa 15.04 18:00-18:30
 * VIVA:  Mo - Fr, 20:30 - 21:00 Uhr // Mo - Fr, 14:30 - 15:00 Uhr (also w/o date)
 *        Mo 21h; Mi 17h (Wh); So 15h
 *        Di, 18:30 - 19:00 h
 *        Sa. 15.4. 17:30h
 * arte:  Fr 14.04. 00.40 [VPS  0035] bis 20.40 (double-height)
 * 3sat:  Freitag, 14. April // 06.20 - 07.00 Uhr
 * CNN:   Sunday 23 April, 13:00 & 19:30 CET
 *        Saturday 22, Sunday 23 April
 *        daily at 11:00 CET
 * MDR:   Sonnabend, 15.04.06 // 00.00 - 02.55 Uhr
 * KiKa:  14. April 2006 // 06.40-07.05 Uhr
 * SWR:   14. April, 00.55 - 02.50 Uhr
 * WDR:   Freitag, 14.04.06   13.40-14.40
 * BR3:   Freitag, 14.04.06 // 13.30-15.05
 *        12.1., 19.45 
 * HR3:   Montag 8.45-9.15 Uhr
 *        Montag // 9.15-9.45 (i.e. 2-line format)
 * Kabl1: Fr 14.04 20:15-21:11
 * Tele5: Fr 19:15-20:15
 */

bool T_PG_DATE::ParseDescDate(int page, int sub, time_t ov_start_t, int date_off) const
{
   int lmday = -1;
   int lmonth = -1;
   int lyear = -1;
   int lhour = -1;
   int lmin = -1;
   int lend_hour = -1;
   int lend_min = -1;
   bool check_time = false;

   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   int lang = pgtext->get_lang();
   string wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
   string wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
   string mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
   smatch whats;

   // search date and time
   for (int line = 1; line <= 23; line++) {
      const string& text = pgtext->get_text(line);
      bool new_date = false;

      {
         // [Fr] 14.04.[06] (year not optional for single-digit day/month)
         static const regex expr1("(^| )(\\d{1,2})\\.(\\d{1,2})\\.(\\d{4}|\\d{2})?( |,|;|:|$)");
         static const regex expr2("(^| )(\\d{1,2})\\.(\\d)\\.(\\d{4}|\\d{2})( |,|;|:|$)");
         static const regex expr3("(^| )(\\d{1,2})\\.(\\d)\\.?(\\d{4}|\\d{2})?(,|;| ?-)? +\\d{1,2}[\\.:]\\d{2}(h| |,|;|:|$)");
         if (   regex_search(text, whats, expr1)
             || regex_search(text, whats, expr2)
             || regex_search(text, whats, expr3)) {
            if (CheckDate(atoi_substr(whats[2]), atoi_substr(whats[3]), atoi_substr(whats[4]),
                          "", "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // Fr 14.04 (i.e. no dot after month)
         // here's a risk to match on a time, so we must require a weekday name
         static regex expr4[8];
         static regex expr5[8];
         if (expr4[lang].empty()) {
            expr4[lang].assign(string("(^| )(") + wday_match + ")(, ?| | - )"
                               "(\\d{1,2})\\.(\\d{1,2})( |\\.|,|;|$)", regex::icase);
            expr5[lang].assign(string("(^| )(") + wday_abbrv_match + ")(\\.,? ?|, ?| - | )"
                               "(\\d{1,2})\\.(\\d{1,2})( |\\.|,|;|:|$)", regex::icase);
         }
         if (   regex_search(text, whats, expr4[lang])
             || regex_search(text, whats, expr5[lang])) {
            if (CheckDate(atoi_substr(whats[4]), atoi_substr(whats[5]), -1,
                          string(whats[2]), "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // 14.[ ]April [2006]
         static regex expr6[8];
         if (expr6[lang].empty()) {
            expr6[lang].assign(string("(^| )(\\d{1,2})\\.? ?(") + mname_match +
                               ")( (\\d{4}|\\d{2}))?( |,|;|:|$)", regex::icase);
         }
         if (regex_search(text, whats, expr6[lang])) {
            if (CheckDate(atoi_substr(whats[2]), -1, atoi_substr(whats[5]),
                          "", string(whats[3]), pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // Sunday 22 April (i.e. no dot after day)
         static regex expr7[8];
         static regex expr8[8];
         if (expr7[lang].empty()) {
            expr7[lang].assign(string("(^| )(") + wday_match +
                               string(")(, ?| - | )(\\d{1,2})\\.? ?(") + mname_match +
                               ")( (\\d{4}|\\d{2}))?( |,|;|:|$)", regex::icase);
            expr8[lang].assign(string("(^| )(") + wday_abbrv_match +
                               string(")(\\.,? ?|, ?| ?- ?| )(\\d{1,2})\\.? ?(") + mname_match +
                               ")( (\\d{4}|\\d{2}))?( |,|;|:|$)", regex::icase);
         }
         if (   regex_search(text, whats, expr7[lang])
             || regex_search(text, whats, expr8[lang])) {
            if (CheckDate(atoi_substr(whats[4]), -1, atoi_substr(whats[7]),
                          string(whats[2]), string(whats[5]), pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // TODO: "So, 23." (n-tv)

         // Fr[,] 18:00 [-19:00] [Uhr|h]
         // TODO parse time (i.e. allow omission of "Uhr")
         static regex expr10[8];
         static regex expr11[8];
         if (expr10[lang].empty()) {
            expr10[lang].assign(string("(^| )(") + wday_match +
                                ")(, ?| - | )(\\d{1,2}[\\.:]\\d{2}"
                                "((-| - )\\d{1,2}[\\.:]\\d{2})?(h| |,|;|:|$))", regex::icase);
            expr11[lang].assign(string("(^| )(") + wday_abbrv_match +
                                ")(\\.,? ?|, ?| - | )(\\d{1,2}[\\.:]\\d{2}"
                                "((-| - )\\d{1,2}[\\.:]\\d{2})?(h| |,|;|:|$))", regex::icase);
         }
         if (   regex_search(text, whats, expr10[lang])
             || regex_search(text, whats, expr11[lang])) {
            if (CheckDate(-1, -1, -1, string(whats[2]), "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // " ... Sonntag" (HR3) (time sometimes directly below, but not always)
         static regex expr20[8];
         if (expr20[lang].empty()) {
            expr20[lang].assign(string("(^| )(") + wday_match + ") *$", regex::icase);
         }
         if (!check_time && regex_search(text, whats, expr20[lang])) {
            if (CheckDate(-1, -1, -1, string(whats[2]), "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // TODO: 21h (i.e. no minute value: TV5)

         // TODO: make exact match between VPS date and time from overview page
         // TODO: allow garbage before or after label; check reveal and magenta codes (ETS 300 231 ch. 7.3)
         // VPS label "1D102 120406 F9"
         static const regex expr21("^ +[0-9A-F]{2}\\d{3} (\\d{2})(\\d{2})(\\d{2}) [0-9A-F]{2} *$");
         if (regex_search(text, whats, expr21)) {
            lmday = atoi_substr(whats[1]);
            lmonth = atoi_substr(whats[2]);
            lyear = atoi_substr(whats[3]) + 2000;
            goto DATE_FOUND;
         }
      }
      DATE_FOUND:

      // TODO: time should be optional if only one subpage
      // 15.40 [VPS 1540] - 19.15 [Uhr|h]
      static const regex expr12("(^| )(\\d{1,2})[\\.:](\\d{1,2})( +VPS +(\\d{4}))?(-| - | bis )(\\d{1,2})[\\.:](\\d{1,2})(h| |,|;|:|$)");
      static const regex expr13("(^| )(\\d{2})h(\\d{2})( +VPS +(\\d{4}))?(-| - )(\\d{2})h(\\d{2})( |,|;|:|$)");
      // 15.40 (Uhr|h)
      static const regex expr14("(^| )(\\d{1,2})[\\.:](\\d{1,2})( |,|;|:|$)");
      static const regex expr15("(^| )(\\d{1,2})[\\.:](\\d{1,2}) *$");
      static const regex expr16("(^| )(\\d{1,2})[\\.:](\\d{1,2})( ?h| Uhr)( |,|;|:|$)");
      static const regex expr17("(^| )(\\d{1,2})h(\\d{2})( |,|;|:|$)");
      check_time = false;
      if (   regex_search(text, whats, expr12)
          || regex_search(text, whats, expr13)) {
         int hour = atoi_substr(whats[2]);
         int min = atoi_substr(whats[3]);
         int end_hour = atoi_substr(whats[7]);
         int end_min = atoi_substr(whats[8]);
         // int vps = atoi_substr(whats[5]);
         if (opt_debug) printf("DESC TIME %02d.%02d - %02d.%02d\n", hour, min, end_hour, end_min);
         if ((hour < 24) && (min < 60) &&
             (end_hour < 24) && (end_min < 60)) {
            lhour = hour;
            lmin = min;
            lend_hour = end_hour;
            lend_min = end_min;
            check_time = true;
         }
      }
      // 15.40 (Uhr|h)
      else if (   (new_date && regex_search(text, whats, expr14))
               || regex_search(text, whats, expr15)
               || regex_search(text, whats, expr16)
               || regex_search(text, whats, expr17)) {
         int hour = atoi_substr(whats[2]);
         int min = atoi_substr(whats[3]);
         if (opt_debug) printf("DESC TIME %02d.%02d\n", hour, min);
         if ((hour < 24) && (min < 60)) {
            lhour = hour;
            lmin = min;
            check_time = true;
         }
      }

      if (check_time && (lyear != -1)) {
         // allow slight mismatch of <5min (for ORF2 or TV5: list says 17:03, desc says 17:05)
         struct tm tm;
         memset(&tm, 0, sizeof(struct tm));
         tm.tm_min = lmin;
         tm.tm_hour = lhour;
         tm.tm_mday = lmday;
         tm.tm_mon = lmonth - 1;
         tm.tm_year = lyear - 1900;
         tm.tm_isdst = -1;
         time_t start_t = mktime(&tm);
         if ((start_t != -1) && (abs(start_t - ov_start_t) < 5*60)) {
            // match found
#if 0 //TODO
            if ((lend_hour != -1) && (m_end_hour != -1)) {
               // add end time to the slot data
               // TODO: also add VPS time
               // XXX FIXME: these are never used because stop_t is already calculated!
               m_end_hour = lend_hour;
               m_end_min = lend_min;
            }
#endif
            return true;
         }
         else {
            if (m_date_off + date_off != 0) {
               // try again with compensation by date offset which was found on overview page (usually in time range 0:00 - 6:00)
               tm.tm_mday += m_date_off + date_off;
               start_t = mktime(&tm);
            }
            if ((start_t != -1) && (abs(start_t - ov_start_t) < 5*60)) {
               // match found
               return true;
            }
            else {
               if (opt_debug) {
                  char t1[100];
                  char t2[100];
                  strftime(t1, sizeof(t1), "%Y-%M-%D.%H:%M", localtime(&start_t));
                  strftime(t2, sizeof(t2), "%Y-%M-%D.%H:%M", localtime(&ov_start_t));
                  printf("MISMATCH[date_off:%d+%d]: %s %s\n", m_date_off, date_off, t1, t2);
               }
               lend_hour = -1;
               if (new_date) {
                  // date on same line as time: invalidate both upon mismatch
                  lyear = -1;
               }
            }
         }
      }
   }
   return false;
}

/* ------------------------------------------------------------------------------
 * Convert discrete start times into UNIX epoch format
 * - implies a conversion from local time zone into GMT
 */
time_t T_PG_DATE::convert_time(int hour, int min, int date_off) const
{
   assert (m_year != -1);

   struct tm tm;
   memset(&tm, 0, sizeof(tm));

   tm.tm_hour = hour;
   tm.tm_min  = min;

   tm.tm_mday = m_mday + m_date_off + date_off;
   tm.tm_mon  = m_month - 1;
   tm.tm_year = m_year - 1900;

   tm.tm_isdst = -1;

   return mktime(&tm);
}

/* ------------------------------------------------------------------------------
 * Calculate delta in days between the given programme slot and a discrete date
 * - works both on program "slot" and "pgdate" which have the same member names
 *   (since Perl doesn't have strict typechecking for structs)
 */
int T_PG_DATE::calc_date_delta(const T_PG_DATE& slot2) const
{
   struct tm tm1;
   struct tm tm2;

   memset(&tm1, 0, sizeof(tm1));
   memset(&tm2, 0, sizeof(tm2));

   tm1.tm_mday = m_mday + m_date_off;
   tm1.tm_mon  = m_month - 1;
   tm1.tm_year = m_year - 1900;
   tm1.tm_isdst = -1;

   tm2.tm_mday = slot2.m_mday + slot2.m_date_off;
   tm2.tm_mon  = slot2.m_month - 1;
   tm2.tm_year = slot2.m_year - 1900;
   tm2.tm_isdst = -1;

   time_t date1 = mktime(&tm1);
   time_t date2 = mktime(&tm2);

   // add 2 hours to allow for shorter days during daylight saving time change
   return (date2 + 2*60*60 - date1) / (24*60*60);
}

const char * T_PG_DATE::trace_str() const
{
   static char buf[100];
   sprintf(buf, "%d.%d.%d, DELTA %d", m_mday, m_month, m_year, m_date_off);
   return buf;
}

/* ------------------------------------------------------------------------------
 * Determine channel name and ID from teletext header packets
 * - remove clock, date and page number: rest should be channel name
 */
string ParseChannelName()
{
   map<string,int> ChName;
   string wday_match;
   string wday_abbrv_match;
   string mname_match;
   smatch whats;
   int lang = -1;
   regex expr3[8];
   regex expr4[8];

   for (TTX_DB::const_iterator p = ttx_db.begin(); p != ttx_db.end(); p++) {
      int page = p->first.page();
      if ( (((page>>4)&0xF) <= 9) && ((page&0xF) <= 9) ) {
         if (p->second->get_lang() != lang) {
            lang = p->second->get_lang();
            wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
            wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
            mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
         }

         char pgn[20];
         sprintf(pgn, "%03X", page);
         string hd = p->second->get_text(0);
         // remove page number and time (both are required)
         string::size_type pgn_pos = str_find_word(hd, pgn);
         if (pgn_pos != string::npos) {
            hd.replace(pgn_pos, 3, 3, ' ');

            static regex expr2("^(.*)( \\d{2}[\\.: ]?\\d{2}([\\.: ]\\d{2}) *)");
            if (regex_search(hd, whats, expr2)) {
               hd.erase(whats[1].length());
               // remove date: "Sam.12.Jan" OR "12 Sa Jan" (VOX)
               if (expr3[lang].empty()) {
                  expr3[lang].assign(string("(((") + wday_abbrv_match + string(")|(") + wday_match +
                                     string("))(\\, ?|\\. ?|  ?\\-  ?|  ?)?)?\\d{1,2}(\\.\\d{1,2}|[ \\.](") +
                                     mname_match + "))(\\.|[ \\.]\\d{2,4}\\.?)? *$", regex::icase);
                  expr4[lang].assign(string("\\d{1,2}(\\, ?|\\. ?|  ?\\-  ?|  ?)(((") +
                                     wday_abbrv_match + string(")|(") +
                                     wday_match +
                                     string ("))(\\, ?|\\. ?|  ?\\-  ?|  ?)?)?(\\.\\d{1,2}|[ \\.](") +
                                     mname_match + "))(\\.|[ \\.]\\d{2,4}\\.?)? *$",
                                     regex::icase);
               }
               if (regex_search(hd, whats, expr3[lang])) {
                  hd.erase(whats.position(), whats[0].length());
               }
               else if (regex_search(hd, whats, expr4[lang])) {
                  hd.erase(whats.position(), whats[0].length());
               }

               // remove and compress whitespace
               hd = regex_replace(hd, regex("(^ +| +$)"), "");
               hd = regex_replace(hd, regex("  +"), " ");

               // remove possible "text" postfix
               hd = regex_replace(hd, regex("[ \\.\\-]?text$", regex::icase), "");
               hd = regex_replace(hd, regex("[ \\.\\-]?Text .*"), "");

               if (ChName.find(hd) != ChName.end()) {
                  ChName[hd] += 1;
                  if (ChName[hd] >= 100)
                     break;
               }
               else {
                  ChName[hd] = 1;
               }
            }
         }
      }
   }
   string name;
   if (!ChName.empty()) {
      // search the most frequently seen CNI value 
      int max_cnt = -1;
      for (map<string,int>::iterator p = ChName.begin(); p != ChName.end(); p++) {
         if (p->second > max_cnt) {
            name = p->first;
            max_cnt = p->second;
         }
      }
   }
   return name;
}

