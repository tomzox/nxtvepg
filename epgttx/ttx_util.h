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
 * $Id: ttx_util.h,v 1.6 2011/01/07 12:27:41 tom Exp tom $
 */
#if !defined (__TTX_UTIL_H)
#define __TTX_UTIL_H

template<class IT>
int atoi_substr(const IT& first, const IT& second)
{
   IT p = first;
   int val = 0;
   while (p != second) {
      val = (val * 10) + (*(p++) - '0');
   }
   return val;
}

template<class MATCH>
int atoi_substr(const MATCH& match)
{
   return match.matched ? atoi_substr(match.first, match.second) : -1;
}

template<class IT>
int atox_substr(const IT& first, const IT& second)
{
   IT p = first;
   int val = 0;
   while (p != second) {
      char c = *(p++);
      if ((c >= '0') && (c <= '9'))
         val = (val * 16) + (c - '0');
      else
         val = (val * 16) + ((c - 'A') & ~0x20) + 10;
   }
   return val;
}

template<class MATCH>
int atox_substr(const MATCH& match)
{
   return match.matched ? atox_substr(match.first, match.second) : -1;
}

template<class IT>
void str_blank(const IT& first, const IT& second)
{
   IT p = first;
   while (p != second) {
      *(p++) = ' ';
   }
}

template<class MATCH>
void str_blank(const MATCH& match)
{
   if (match.matched) {
      str_blank(match.first, match.second);
   }
}

// return the current foreground color at the end of the given string
template<class IT>
int str_fg_col(const IT& first, const IT& second)
{
   int fg = 7;

   for (IT p = first; p != second; p++) {
      unsigned char c = *p;
      if (c <= 7) {
         fg = (unsigned int) c;
      }
   }
   return fg;
}

template<class MATCH>
int str_fg_col(const MATCH& match)
{
   return str_fg_col(match.first, match.second);
}

// return TRUE if text is concealed at the end of the given string
template<class IT>
bool str_is_concealed(const IT& first, const IT& second)
{
   bool is_concealed = false;

   for (IT p = first; p != second; p++) {
      unsigned char c = *p;
      if (c == '\x18') {
         is_concealed = true;
      }
      else if (c <= '\x07') {
         is_concealed = false;
      }
   }
   return is_concealed;
}

template<class MATCH>
bool str_is_concealed(const MATCH& match)
{
   return str_is_concealed(match.first, match.second);
}

// return the current background color at the end of the given string
template<class IT>
int str_bg_col(const IT& first, const IT& second)
{
   int fg = 7;
   int bg = 0;

   for (IT p = first; p != second; p++) {
      unsigned char c = *p;
      if (c <= 7) {
         fg = (unsigned int) c;
      }
      else if ((c >= 0x10) && (c <= 0x17)) {
         fg = (unsigned int) c - 0x10;
      }
      else if (c == 0x1D) {
         bg = fg;
      }
   }
   return bg;
}

template<class MATCH>
int str_bg_col(const MATCH& match)
{
   return str_bg_col(match.first, match.second);
}


inline
bool isupper_latin1(char chr)
{
   uint8_t c = chr;
   return (   ((c <= 'Z') && (c >= 'A'))
           || ((c >= 0xC0) && (c <= 0xDE) && (c != 0xD7)) );
}

inline
bool islower_latin1(char chr)
{
   uint8_t c = chr;
   return (   ((c <= 'z') && (c >= 'a'))
           || ((c >= 0xDF) && (c <= 0xFE) && (c != 0xF7)) );
}

inline
char tolower_latin1(char chr)
{
   uint8_t c = chr;

   if ((c <= 'Z') && (c >= 'A'))
      return c + 32;
   else if ((c >= 0xC0) && (c <= 0xDE) && (c != 0xD7))
      return c + 32;
   else
      return c;
}

inline
bool isalnum_latin1(char chr)
{
   uint8_t c = chr;

   if (   ((c >= 'a') && (c <= 'z'))
       || ((c >= 'A') && (c <= 'Z'))
       || ((c >= '0') && (c <= '9'))
       || ((c >= 0xC0) && (c != 0xD7) && (c != 0xF7)) )
     return true;
   else
     return false;
}

void str_tolower_latin1(string& str, unsigned pos);
bool str_all_upper(string& str);
void str_repl_ctrl(string& str);
bool str_concat_title(string& title, const string& str2, bool if_cont_only);
bool str_cmp_alnum(const string& str1, const string& str2, unsigned * p_pos1, unsigned * p_pos2);
unsigned str_len_alnum(const string& str);
bool str_is_left_word_boundary(const string& str, unsigned pos);
bool str_is_right_word_boundary(const string& str, unsigned pos);
string::size_type str_find_word(const string& str, const string& word);
unsigned str_get_indent(const string& str);
void str_chomp(string& str);

#if defined (USE_TTX_GRABBER) // nxtvepg
#define opt_debug false
#else
extern int opt_debug;
#endif

#endif // __TTX_UTIL_H
