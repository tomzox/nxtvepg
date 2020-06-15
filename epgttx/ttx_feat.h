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
 * Copyright 2006-2011,2020 by T. Zoerner (tomzo at users.sf.net)
 */
#if !defined (__TTX_FEAT_H)
#define __TTX_FEAT_H

class TV_FEAT
{
public:
   TV_FEAT() : m_flags(0) {}
   void set_tip(bool v) { if (v) m_flags |= (1 << TV_FEAT_TIP); };
   void ParseTrailingFeat(string& title);

private:
   enum TV_FEAT_TYPE
   {
      TV_FEAT_SUBTITLES,
      TV_FEAT_2CHAN,
      TV_FEAT_ASPECT_16_9,
      TV_FEAT_BW,
      TV_FEAT_HD,
      TV_FEAT_DOLBY,
      TV_FEAT_MONO,
      TV_FEAT_OMU,
      TV_FEAT_STEREO,
      TV_FEAT_TIP,
      TV_FEAT_COUNT
   };
   struct TV_FEAT_STR
   {
      const char         * p_name;
      TV_FEAT_TYPE         type;
   };
   static const TV_FEAT_STR FeatToFlagMap[];

   void MapTrailingFeat(const char * feat, int len, const string& title);
   friend TV_FEAT GetXmltvFeat(const string& xml);

private:
   unsigned m_flags;

public:
   bool has_subtitles()  const { return ((m_flags & (1 << TV_FEAT_SUBTITLES)) != 0); }
   bool is_2chan()       const { return ((m_flags & (1 << TV_FEAT_2CHAN)) != 0); }
   bool is_aspect_16_9() const { return ((m_flags & (1 << TV_FEAT_ASPECT_16_9)) != 0); }
   bool is_video_bw()    const { return ((m_flags & (1 << TV_FEAT_BW)) != 0); }
   bool is_video_hd()    const { return ((m_flags & (1 << TV_FEAT_HD)) != 0); }
   bool is_dolby()       const { return ((m_flags & (1 << TV_FEAT_DOLBY)) != 0); }
   bool is_mono()        const { return ((m_flags & (1 << TV_FEAT_MONO)) != 0); }
   bool is_omu()         const { return ((m_flags & (1 << TV_FEAT_OMU)) != 0); }
   bool is_stereo()      const { return ((m_flags & (1 << TV_FEAT_STEREO)) != 0); }
   bool is_tip()         const { return ((m_flags & (1 << TV_FEAT_TIP)) != 0); }
};

#endif // __TTX_FEAT_H
