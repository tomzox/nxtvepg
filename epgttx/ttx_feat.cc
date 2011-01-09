/*
 * Teletext EPG grabber: Feature keyword parser
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
 * $Id: ttx_feat.cc,v 1.5 2011/01/06 16:59:34 tom Exp tom $
 */

#include <stdio.h>
#include <string.h>

#include <string>

#include <boost/regex.h>
#include <boost/regex.hpp>

using namespace std;
using namespace boost;

#include "ttx_util.h"
#include "ttx_feat.h"

/* ------------------------------------------------------------------------------
 * Parse feature indicators in the overview table
 * - it's mandatory to remove features becaue otherwise they might show up in
 *   the middle of a wrapped title
 * - most feature strings are unique enough so that they can be identified
 *   by a simple pattern match (e.g. 16:9, UT)
 * - TODO: use color to identify features; make a first sweep to check if
 *   most unique features always have a special color (often yellow) and then
 *   remove all words in that color inside title strings

 * Examples:
 *
 * 06.05 # logo!                     >331
 *
 * 13.15  1315  Heilkraft aus der Wüste   
 *              (1/2) Geheimnisse der     
 *              Buschmänner 16:9 oo       
 *
 * TODO: some tags need to be localized (i.e. same as month names etc.)
 */

const TV_FEAT::TV_FEAT_STR TV_FEAT::FeatToFlagMap[] =
{
   { "untertitel", TV_FEAT_SUBTITLES },
   { "ut", TV_FEAT_SUBTITLES },
   { "omu", TV_FEAT_OMU },
   { "sw", TV_FEAT_BW },
   { "s/w", TV_FEAT_BW },
   { "hd", TV_FEAT_HD },
   { "breitbild", TV_FEAT_ASPECT_16_9 },
   { "16:9", TV_FEAT_ASPECT_16_9 },
   { "°°", TV_FEAT_2CHAN }, // according to ARD page 395
   { "oo", TV_FEAT_STEREO },
   { "stereo", TV_FEAT_STEREO },
   { "ad", TV_FEAT_2CHAN }, // accoustic description
   { "hörfilm", TV_FEAT_2CHAN },
   { "hörfilm°°", TV_FEAT_2CHAN },
   { "hf", TV_FEAT_2CHAN },
   { "2k", TV_FEAT_2CHAN },
   { "2k-ton", TV_FEAT_2CHAN },
   { "dolby", TV_FEAT_DOLBY },
   { "surround", TV_FEAT_DOLBY },
   { "stereo", TV_FEAT_STEREO },
   { "mono", TV_FEAT_MONO },
   { "tipp", TV_FEAT_TIP },
   { "tipp!", TV_FEAT_TIP },
   // ORF
   //{ "°°", TV_FEAT_STEREO }, // conflicts with ARD
   { "°*", TV_FEAT_2CHAN },
   { "ds", TV_FEAT_DOLBY },
   { "ss", TV_FEAT_DOLBY },
   { "dd", TV_FEAT_DOLBY },
   { "zs", TV_FEAT_2CHAN },
};
#define TV_FEAT_MAP_LEN (sizeof(FeatToFlagMap)/sizeof(FeatToFlagMap[0]))

void TV_FEAT::MapTrailingFeat(const char * feat, int len, const string& title)
{
   if ((len >= 2) && (strncasecmp(feat, "ut", 2) == 0)) // TV_FEAT_SUBTITLES
   {
      if (opt_debug) printf("FEAT \"%s\" -> on TITLE %s\n", feat, title.c_str());
      m_flags |= 1 << TV_FEAT_SUBTITLES;
      return;
   }

   for (unsigned idx = 0; idx < TV_FEAT_MAP_LEN; idx++)
   {
      if (strncasecmp(feat, FeatToFlagMap[idx].p_name, len) == 0)
      {
         assert(FeatToFlagMap[idx].type < TV_FEAT_COUNT);
         m_flags |= (1 << FeatToFlagMap[idx].type);

         if (opt_debug) printf("FEAT \"%s\" -> on TITLE %s\n", feat, title.c_str());
         return;
      }
   }
   if (opt_debug) printf("FEAT dropping \"%s\" on TITLE %s\n", feat, title.c_str());
}

// note: must correct $n below if () are added to pattern
#define FEAT_PAT_STR "UT(( auf | )?[1-8][0-9][0-9])?|" \
                     "[Uu]ntertitel|[Hh]örfilm(°°)?|HF|AD|" \
                     "s/?w|S/?W|tlw. s/w|oo|°°|°\\*|OmU|" \
                     "4:3|16:9|HD|[Bb]reitbild|" \
                     "2K|2K-Ton|[Mm]ono|[Ss]tereo|[Dd]olby|[Ss]urround|" \
                     "DS|SS|DD|ZS|" \
                     "Wh\\.?|Wdh\\.?|Whg\\.?|Tipp!?|\\d{2,3} (MIN|Min|min)\\.?"

void TV_FEAT::ParseTrailingFeat(string& title)
{
   string orig_title = regex_replace(title, regex("[\\x00-\\x1F\\x7F]"), " ");
   smatch whats;
   // note: remove trailing space here, not in the loop to allow max. 1 space between features
   static const regex expr2("[ \\x00-\\x1F\\x7F]+$");
   title = regex_replace(title, expr2, "");

   // ARTE: (oo) (2K) (Mono)
   // SWR: "(16:9/UT)", "UT 150", "UT auf 150"
   static const regex expr3("^(.*?)[ \\x00-\\x07\\x1D]"
                            "\\(((" FEAT_PAT_STR ")(( ?/ ?|, ?| )(" FEAT_PAT_STR "))*)\\)$");
   static const regex expr4("^(.*?)(  |[\\x00-\\x07\\x1D])[\\x00-\\x07\\x1D ]*"
                            "((" FEAT_PAT_STR ")"
                            "((, ?|/ ?| |[,/]?[\\x00-\\x07\\x1D]+)(" FEAT_PAT_STR "))*)$");

   if (regex_search(title, whats, expr3)) {
      string feat_list = string(whats[2].first, whats[2].second);

      MapTrailingFeat(feat_list.c_str(), whats[3].length(), orig_title);
      feat_list.erase(0, whats[3].length());

      // delete everything following the first sub-match
      title.erase(whats[1].length());

      if (feat_list.length() != 0)  {
         static const regex expr5("( ?/ ?|, ?| )(" FEAT_PAT_STR ")$");
         while (regex_search(feat_list, whats, expr5)) {
            MapTrailingFeat(&whats[2].first[0], whats[2].length(), orig_title);
            feat_list.erase(feat_list.length() - whats[0].length());
         } 
      }
      else {
         static const regex expr6("[ \\x00-\\x07\\x1D]*\\((" FEAT_PAT_STR ")\\)$");
         while (regex_search(title, whats, expr6)) {
            MapTrailingFeat(&whats[1].first[0], whats[1].length(), orig_title);
            title.erase(title.length() - whats[0].length());
         } 
      }
   }
   else if (regex_search(title, whats, expr4)) {
      string feat_list = string(whats[3]);

      MapTrailingFeat(feat_list.c_str(), whats[4].length(), orig_title);
      feat_list.erase(0, whats[4].length());

      // delete everything following the first sub-match
      title.erase(whats[1].length());

      static const regex expr7("([,/][ \\x00-\\x07\\x1D]*|[ \\x00-\\x07\\x1D]+)(" FEAT_PAT_STR ")$");
      while (regex_search(feat_list, whats, expr7)) {
         MapTrailingFeat(&whats[2].first[0], whats[2].length(), orig_title);
         feat_list.erase(feat_list.length() - whats[0].length());
      } 
   } //else { print "NONE orig_title\n"; }

   // compress adjacent white-space & replace special chars with blanks
   static const regex expr8("[\\x00-\\x1F\\x7F ]+");
   title = regex_replace(title, expr8, " ");

   // remove leading and trailing space
   str_chomp(title);
}

