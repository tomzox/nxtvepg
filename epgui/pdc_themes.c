/*
 *  PDC theme names definition (English language only)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation. You find a copy of this
 *  license in the file COPYRIGHT in the root directory of this release.
 *
 *  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
 *  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
 *  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *
 *  Description: see according C source file.
 *
 *    This table conforms to the programme type principle of classification
 *    as defined in in ETS 300 231 (PDC) and ETS 300 707 (Nextview EPG)
 *    See http://www.etsi.org/
 *
 *  Author: Tom Zoerner
 *
 *  $Id: pdc_themes.c,v 1.5 2001/02/25 16:03:47 tom Exp tom $
 */

#define __PDC_THEMES_C

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgui/pdc_themes.h"

const uchar * const pdc_themes[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "movie - general",
   /*0x11*/ "detective/thriller",
   /*0x12*/ "adventure/western/war",
   /*0x13*/ "sci-fi/fantasy/horror",
   /*0x14*/ "comedy",
   /*0x15*/ "melodrama/folklore",
   /*0x16*/ "romance",
   /*0x17*/ "historical drama",
   /*0x18*/ "adult movie",
            0,0,0,0,0,0,0,
   /*0x20*/ "news - general",
   /*0x21*/ "news",
   /*0x22*/ "news magazine",
   /*0x23*/ "documentary",
   /*0x24*/ "discussion/interview/debate",

   /*0x25*/ "social/political/economics - general",
   /*0x26*/ "news magazines/reports",
   /*0x27*/ "economics/social advisory",
   /*0x28*/ "remarkable people",
            0,0,0,0,0,0,0,
   /*0x30*/ "show/game show - general",
   /*0x31*/ "game/show/quiz/contest",
   /*0x32*/ "variety show",
   /*0x33*/ "talk show",

   /*0x34*/ "leisure hobbies - general",
   /*0x35*/ "tourism/travel",
   /*0x36*/ "handicraft",
   /*0x37*/ "motoring",
   /*0x38*/ "fitness and health",
   /*0x39*/ "cooking",
   /*0x3A*/ "advertisement/shopping",
            0,0,0,0,0,
   /*0x40*/ "sports - general",
   /*0x41*/ "special sports events",
   /*0x42*/ "sports magazines",
   /*0x43*/ "football & soccer",
   /*0x44*/ "tennis & squash",
   /*0x45*/ "misc. team sports",
   /*0x46*/ "athletics",
   /*0x47*/ "motor sports",
   /*0x48*/ "water sports",
   /*0x49*/ "winter sports",
   /*0x4A*/ "equestrian",
   /*0x4B*/ "martial arts",
   /*0x4C*/ "local sports",
            0,0,0,
   /*0x50*/ "kids - general",
   /*0x51*/ "pre-school",
   /*0x52*/ "kids from 6 to 14",
   /*0x53*/ "kids from 10 to 16",
   /*0x54*/ "kids educational",
   /*0x55*/ "cartoons & puppets",

   /*0x56*/ "science - general",
   /*0x57*/ "nature",
   /*0x58*/ "technology",
   /*0x59*/ "medicine",
   /*0x5A*/ "foreign",
   /*0x5B*/ "social",
   /*0x5C*/ "misc. education",
   /*0x5D*/ "languages",
            0,0,
   /*0x60*/ "music/ballet/dance - general",
   /*0x61*/ "rock & pop",
   /*0x62*/ "serious & classical music",
   /*0x63*/ "folk & traditional music",
   /*0x64*/ "jazz",
   /*0x65*/ "musical & opera",
   /*0x66*/ "ballet",
            0,0,0,0,0,0,0,0,0,
   /*0x70*/ "arts/culture - general",
   /*0x71*/ "performing arts",
   /*0x72*/ "fine arts",
   /*0x73*/ "religion",
   /*0x74*/ "pop culture/traditional arts",
   /*0x75*/ "literature",
   /*0x76*/ "film & cinema",
   /*0x77*/ "experimental film/video",
   /*0x78*/ "broadcasting & press",
   /*0x79*/ "new media",
   /*0x7A*/ "arts & culture magazines",
   /*0x7B*/ "fashion",
            0,0,0,0,
   /*0x80*/ "series - general",
};


// The codes above 0x80 are defined indiviually for each network
// The names for these codes are implied by the titles of the
// assigned programme entries, i.e. PI blocks.
const uchar * const pdc_series = "series";

const uchar * const pdc_undefined_theme = "undefined";

const uchar pdc_categories[] =
{
   0x10, // movie - general
   0x20, // news - general
   0x25, // social/political/economics - general
   0x30, // show/game show - general
   0x34, // leisure hobbies - general
   0x40, // sports - general
   0x50, // kids - general
   0x56, // science - general
   0x60, // music/ballet/dance - general
   0x70, // arts/culture - general
   0x80, // series - general
   0
};

// ---------------------------------------------------------------------------
// Determine in which category the given theme falls
//
uchar PdcThemeGetCategory( uchar theme )
{
   int idx, category;

   category = theme;
   for (idx=0; pdc_categories[idx] != 0; idx++)
   {
      if (pdc_categories[idx + 1] > theme)
      {
         category = pdc_categories[idx];
         break;
      }
   }

   return category;
}
