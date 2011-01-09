/*
 * Teletext EPG grabber: Miscellaneous helper functions
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
 * $Id: ttx_util.cc,v 1.7 2011/01/06 11:11:07 tom Exp tom $
 */

#include <stdio.h>
#include <string.h>

#include <string>
#include <algorithm>

#include "boost/regex.h"
#include "boost/regex.hpp"

using namespace std;
using namespace boost;

#include "ttx_util.h"

/* Turn the entire given string into lower-case.
 */
void str_tolower_latin1(string& str, unsigned pos)
{
   if (pos < str.length()) {
      for (string::iterator p = str.begin() + pos; p < str.end(); p++) {
         *p = tolower_latin1(*p);
      }
   }
}

/* Check if all letters in the string are upper-case
 * AND if at least two letters are in the string (i.e. exclude "K2" and "D.A.R.P")
 */
bool str_all_upper(string& str)
{
   bool two = false;

   for (string::iterator p = str.begin(); p != str.end(); ++p) {
      // check for at least two consecutive upper-case letters
      if (isupper_latin1(*p)) {
         ++p;
         if (p == str.end()) {
            return two;
         }
         if (isupper_latin1(*p)) {
            two = true;
            continue;
         }
      }
      // abort upon any lower-case letters
      if (islower_latin1(*p)) {
         return false;
      }
   }
   return two;
}

/* Replace all TTX control characters with whitespace. Note this is used after
 * G0 to Latin-1 conversion, so there may be 8-bit chars.
 */
void str_repl_ctrl(string& str)
{
   for (unsigned idx = 0; idx < str.length(); idx++) {
      unsigned char c = str[idx];
      if ((c <= 0x1F) || (c == 0x7F))
         str[idx] = ' ';
   }
}

/* Append the second sring to the first one, with exactly one blank
 * in-between. Exception: if the first string ends in "-" and the
 * second starts lower-case, remove the "-" and append w/o blank.
 * Note this function is called multiple times to concatenate multiple
 * lines, until the function returns false.
 */
bool str_concat_title(string& title, const string& str2, bool if_cont_only)
{
   bool result = false;

   // count whitespace at the end of the first string
   string::reverse_iterator p_end = title.rbegin();
   unsigned del1 = 0;
   while (   (p_end != title.rend())
          && (uint8_t(*p_end) <= ' ') )
   {
      ++p_end;
      ++del1;
   }

   // skip whitespace at the start of the second string
   string::const_iterator p_start = str2.begin();
   while (   (p_start != str2.end())
          && (uint8_t(*p_start) <= ' ') )
   {
      ++p_start;
   }

   // remove whitespace and "-" at the end of the first string
   if (   (title.length() > 2 + del1)
       && (title[title.length() - del1 - 1] == '-')
       && (title[title.length() - del1 - 2] != ' ')
       && (p_start != str2.end())
       && islower_latin1(*p_start) )
   {
      // remove (replace) the "-" and
      // append (replace) the second string, skipping whitespace
      title.replace(title.end() - del1 - 1, title.end(),
                    p_start, str2.end());
      result = true;
   }
   else if (   (title.length() > 2 + del1)
            && (   (title[title.length() - del1 - 1] == ':')
                || (title[title.length() - del1 - 1] == ',')
                || (title[title.length() - del1 - 1] == '&')
                || (title[title.length() - del1 - 1] == '-') )
            && (p_start != str2.end()) )
   {
      // keep the ':' plus a single whitespace, then append the second string
      // unless there's a hyphen without leading space, then add no blank after
      if (   (title[title.length() - del1 - 1] == '-')
          && (title[title.length() - del1 - 2] != ' '))
        title.erase(title.end() - del1, title.end());
      else
        title.replace(title.end() - del1, title.end(), " ");
      title.append(p_start, str2.end());
      result = true;
   }
   else if (!if_cont_only)
   {
      if (del1 > 0)
         title.erase(title.end() - del1);

      if ((title.length() > 0) && (p_start != str2.end()))
         title.append(1, ' ');

      // append the second string, skipping whitespace
      title.append(p_start, str2.end());
   }
   return result;
}

/* Check if there's a word boundary in the given string beginning
 * at the character with the given position (i.e. the character before
 * the given position is the last alpha-numeric character.)
 */
bool str_is_left_word_boundary(const string& str, unsigned pos)
{
   return (pos == 0) || !isalnum_latin1(str[pos - 1]);
}

/* Check if there's a word boundary in the given string before the
 * character with the given position.
 */
bool str_is_right_word_boundary(const string& str, unsigned pos)
{
   return (pos >= str.length()) || !isalnum_latin1(str[pos]);
}

/* Search for the string "word" inside of "str". The match must be
 * located at the beginning or end, or the adjacent characters must be
 * non-alphanumeric (i.e. word boundaries)
 */
string::size_type str_find_word(const string& str, const string& word)
{
   string::size_type pos = 0;
   do {
      pos = str.find(word, pos);

      if (pos != string::npos) {
         if (   str_is_left_word_boundary(str, pos)
             && str_is_right_word_boundary(str, pos + word.length()) ) {
            return pos;
         }
      } else {
         break;
      }
   } while (++pos + word.length() < str.length());

   return string::npos;
}

/* Compare the two strings, but ignore differences in white-space and
 * punctuation and case.  Returns TRUE if the strings are equivalent, else
 * FALSE. If the pointers are non-null, the function also returns the indices
 * of the first differing characters in either string.
 */
bool str_cmp_alnum(const string& str1, const string& str2, unsigned * p_pos1, unsigned * p_pos2)
{
   unsigned pos1 = 0;
   unsigned pos2 = 0;
   bool result = true;

   while (1) {
      // skip non-alnum chars
      while ((pos1 < str1.length()) && !isalnum_latin1(str1[pos1]))
         pos1++;
      while ((pos2 < str2.length()) && !isalnum_latin1(str2[pos2]))
         pos2++;
      // terminate upon end of string: fail if only one string is ended
      if (pos1 >= str1.length()) {
         if (pos2 < str2.length())
            result = false;
         break;
      }
      else if (pos2 >= str2.length()) {
         result = false;
         break;
      }
      // compare the next character
      uint8_t c1 = str1[pos1];
      uint8_t c2 = str2[pos2];
      if ((c1 != c2) && (tolower_latin1(c1) != tolower_latin1(c2))) {
         result = false;
         break;
      }
      pos1++;
      pos2++;
   }
   if (p_pos1 != 0)
      *p_pos1 = pos1;
   if (p_pos2 != 0)
      *p_pos2 = pos2;
   return result;
}

/* Returns the length of the string, after skipping non-alphanumeric characters
 * at the beginning and end.
 */
unsigned str_len_alnum(const string& str)
{
   unsigned off = 0;
   unsigned del = 0;

   while ((off < str.length()) && !isalnum_latin1(str[off]))
      off++;

   if (off < str.length()) {  // implies length > 0
      del = str.length() - 1;
      while ((del > off) && !isalnum_latin1(str[del]))
         del--;
      del = str.length() - 1 - del;
   }

   return str.length() - off - del;
}

/* Count and return the number of blank (or control) characters at the beginning
 * of the given string.
 */
unsigned str_get_indent(const string& str)
{
   unsigned off;

   for (off = 0; off < str.length(); off++) {
      uint8_t c = str[off];
      if (c > ' ')  // not ctrl char or space
         break;
   }
   return off;
}

/* Remove white-space from the beginning and end of the given string.
 */
void str_chomp(string& str)
{
   if (str.length() > 0) {
      string::iterator end = str.begin() + str.length() - 1;
      while ((str.begin() != end) && (uint8_t(*end) <= ' ')) {
         end--;
      }
      str.erase(end + 1, str.end());
   }

   string::iterator begin = str.begin();
   while ((begin != str.end()) && (uint8_t(*begin) <= ' ')) {
      begin++;
   }
   str.erase(str.begin(), begin);
}

