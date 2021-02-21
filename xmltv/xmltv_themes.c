/*
 *  Helper module to map theme strings to PDC codes
 *
 *  Copyright (C) 2007-2011, 2020-2021 T. Zoerner
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
 *  Description:
 *
 *    This modules receives a string of text which it then maps against
 *    PDC theme category names.  It returns main and sub-category, or just
 *    a main category.
 *
 *    To identify a theme, a lot of string comparisons may be necessary,
 *    so the caller should cache the result, e.g. in a hash array.
 */

#define DEBUG_SWITCH DEBUG_SWITCH_XMLTV
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "xmltv/xmltv_themes.h"


#define XMLTV_ADD_THEME_CODE(CAT, THEME) \
                { pCache->cat = (CAT); \
                  pCache->theme = (THEME); \
                  goto done; }

// ----------------------------------------------------------------------------
// German theme names
//
void Xmltv_ParseThemeStringGerman( HASHED_THEMES * pCache, const char * pStr )
{
   if ( (strstr(pStr, "serie") != NULL) ||
        (strstr(pStr, "reihe") != NULL) )
      XMLTV_ADD_THEME_CODE(0x80, 0);

   if ( (strstr(pStr, "krimi") != NULL) ||
        (strstr(pStr, "polizei") != NULL) ||
        (strstr(pStr, "detektiv") != NULL) ||
        (strstr(pStr, "thriller") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x11);
   if ( (strstr(pStr, "krieg") != NULL) ||
        (strstr(pStr, "western") != NULL) ||
        (strstr(pStr, "abenteuer") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x12);
   if ( (strstr(pStr, "fiction") != NULL) ||
        (strstr(pStr, "science") != NULL) ||
        (strstr(pStr, "fantas") != NULL) ||
        (strstr(pStr, "horror") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x13);
   if ( (strstr(pStr, "komöd") != NULL) ||
        (strstr(pStr, "comedy") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x14);
   if (strstr(pStr, "drama") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0x15);
   if ( (strstr(pStr, "romanti") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x16);
   if ( (strstr(pStr, "histor") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x17);
   if ( (strstr(pStr, "eroti") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x18);
   if (strstr(pStr, "film") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0);

   if (strstr(pStr, "wetter") != NULL)
      XMLTV_ADD_THEME_CODE(0x20, 0x21);
   if ( (strstr(pStr, "dokument") != NULL) ||
        (strstr(pStr, "document") != NULL) )
      XMLTV_ADD_THEME_CODE(0x20, 0x23);
   if ( (strstr(pStr, "diskussion") != NULL) ||
        (strstr(pStr, "interview") != NULL) ||
        (strstr(pStr, "debatte") != NULL) )
      XMLTV_ADD_THEME_CODE(0x20, 0x24);
   if ( (strstr(pStr, "nachricht") != NULL) ||
        (strstr(pStr, "news") != NULL) )
   {
      if (strstr(pStr, "magazin") != NULL)
         XMLTV_ADD_THEME_CODE(0x25, 0x26);
      XMLTV_ADD_THEME_CODE(0x20, 0);
   }

   if (strstr(pStr, "bericht") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x26);
   if (strstr(pStr, "ratgeber") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x27);
   if (strstr(pStr, "persönlichkeit") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x28);
   if ( (strstr(pStr, "wirtschaft") != NULL) ||
        (strstr(pStr, "politik") != NULL) ||
        (strstr(pStr, "sozial") != NULL) )
      XMLTV_ADD_THEME_CODE(0x25, 0);

   if (strstr(pStr, "talk") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0x33);
   if ( (strstr(pStr, "quiz") != NULL) ||
        (strstr(pStr, "spiel") == pStr) || // only allowed at start, else conflict with "Puppenspiel"
        (strstr(pStr, "wettstreit") != NULL) )
      XMLTV_ADD_THEME_CODE(0x30, 0x31);
   if (strstr(pStr, "variet") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0x32);
   if ( (strstr(pStr, "game") != NULL) ||
        (strstr(pStr, "show") != NULL) )
      XMLTV_ADD_THEME_CODE(0x30, 0);

   if ( (strstr(pStr, "reise") != NULL) ||
        (strstr(pStr, "tourism") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x35);
   if (strstr(pStr, "handarbeit") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x36);
   if ( (strstr(pStr, "auto") != NULL) ||
        ((strstr(pStr, "motor") != NULL) && (strstr(pStr, "sport") == NULL)) )
      XMLTV_ADD_THEME_CODE(0x34, 0x37);
   if ( (strstr(pStr, "fitness") != NULL) ||
        (strstr(pStr, "gesundheit") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x38);
   if (strstr(pStr, "kochen") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x39);
   if ( (strstr(pStr, "shopping") != NULL) ||
        (strstr(pStr, "werbung") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x3a);
   if ( (strstr(pStr, "hobby") != NULL) ||
        (strstr(pStr, "hobbies") != NULL) ||
        (strstr(pStr, "freizeit") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0);

   if (strstr(pStr, "fußball") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x43);
   if ( (strstr(pStr, "tennis") != NULL) ||
        (strstr(pStr, "squash") != NULL) )
      XMLTV_ADD_THEME_CODE(0x40, 0x44);
   if (strstr(pStr, "atletik") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x46);
   if (strstr(pStr, "pferde") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x4a);
   if (strstr(pStr, "kampfsport") != NULL)  // match only combined words
      XMLTV_ADD_THEME_CODE(0x40, 0x4b);
   if (strstr(pStr, "sport") != NULL)
   {
      if (strstr(pStr, "motor") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x47);
      if (strstr(pStr, "wasser") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x48);
      if (strstr(pStr, "winter") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x49);
      if (strstr(pStr, "magazin") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x42);
      if (strstr(pStr, "team") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x45);
      XMLTV_ADD_THEME_CODE(0x40, 0);
   }
   if ( (strstr(pStr, "fahrrad") != NULL) ||
        (strstr(pStr, "golf") != NULL) ||
        (strncmp(pStr, "boxen", 4) == 0) ||
        (strncmp(pStr, "boxkampf", 4) == 0) ||
        (strncmp(pStr, "ski", 3) == 0) )
      XMLTV_ADD_THEME_CODE(0x40, 0);

   if (strstr(pStr, "vorschul") != NULL)
      XMLTV_ADD_THEME_CODE(0x50, 0x51);
   if (strstr(pStr, "puppenspiel") != NULL)
      XMLTV_ADD_THEME_CODE(0x50, 0x55);
   if (strstr(pStr, "zeichentrick") != NULL)
      XMLTV_ADD_THEME_CODE(0x55, 0); // XXX work-around for PDC bug: not all comics are for children
   if (strstr(pStr, "kinder") != NULL)
   {
      if (strstr(pStr, "bildung") != NULL)
         XMLTV_ADD_THEME_CODE(0x50, 0x54);
      XMLTV_ADD_THEME_CODE(0x50, 0);
   }

   if (strstr(pStr, "natur") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x57);
   if (strstr(pStr, "technologie") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x58);
   if (strstr(pStr, "medizin") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x59);
   if (strstr(pStr, "ausland") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5a);
   if (strstr(pStr, "fortbildung") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5c);
   if (strstr(pStr, "sprachen") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5d);
   if (strstr(pStr, "wissenschaft") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0);

   if (strcmp(pStr, "musik/ballett/tanz") == 0) // general category name
      XMLTV_ADD_THEME_CODE(0x60, 0);
   if (strstr(pStr, "rock") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x61);
   if ( (strstr(pStr, "klassisch") != NULL) &&
        (strstr(pStr, "musik") != NULL) )
      XMLTV_ADD_THEME_CODE(0x60, 0x62);
   if (strstr(pStr, "olksmusik") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x63);
   if (strstr(pStr, "jazz") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x64);
   if ( (strstr(pStr, "oper") != NULL) &&
        (strstr(pStr, "musical") != NULL) )
      XMLTV_ADD_THEME_CODE(0x60, 0x65);
   if (strstr(pStr, "ballett") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x66);
   if (strstr(pStr, "musik") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0);

   if (strstr(pStr, "religion") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x73);
   if (strstr(pStr, "popkultur") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x74);
   if (strstr(pStr, "literatur") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x75);
   if (strstr(pStr, "kino") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x76);
   if ( (strstr(pStr, "experiment") != NULL) &&
        ( (strstr(pStr, "film") != NULL) ||
          (strstr(pStr, "video") != NULL) ))
      XMLTV_ADD_THEME_CODE(0x70, 0x77);
   if (strstr(pStr, "presse") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x78);
   if (strstr(pStr, "mode") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x7b);
   if ( (strstr(pStr, "kunst") != NULL) ||
        (strstr(pStr, "künst") != NULL) ||
        (strstr(pStr, "kultur") != NULL) )
   {
      if (strstr(pStr, "darstellend") != NULL)
         XMLTV_ADD_THEME_CODE(0x70, 0x71);
      if (strstr(pStr, "hohe") != NULL)
         XMLTV_ADD_THEME_CODE(0x70, 0x72);
      XMLTV_ADD_THEME_CODE(0x70, 0);
   }

   dprintf1("Theme-German: '%s': unknown\n", pStr);
   pCache->cat = 0;
   pCache->theme = 0;
   return;

done:
   dprintf3("Theme-German: '%s': 0x%02x,%02x\n", pStr, pCache->cat, pCache->theme);
   return;
}

// ----------------------------------------------------------------------------
// French theme names
//
void Xmltv_ParseThemeStringFrench( HASHED_THEMES * pCache, const char * pStr )
{
   if (strstr(pStr, "séries") != NULL)
      XMLTV_ADD_THEME_CODE(0x80, 0);

   if ( (strstr(pStr, "policier") != NULL) ||
        (strstr(pStr, "thriller") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x11);
   if ( (strstr(pStr, "guerre") != NULL) ||
        (strstr(pStr, "western") != NULL) ||
        (strstr(pStr, "aventure") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x12);
   if ( (strstr(pStr, "fiction") != NULL) ||
        (strstr(pStr, "science") != NULL) ||
        (strstr(pStr, "fantas") != NULL) ||
        (strstr(pStr, "horreur") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x13);
   if (strstr(pStr, "comédie") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0x14);
   if ( (strstr(pStr, "historique") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x17);
   if ( (strstr(pStr, "drame") != NULL) ||
        (strstr(pStr, "folklore") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x15);
   if ( (strstr(pStr, "romance") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x16);
   if ( (strstr(pStr, "adulte") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x18);
   if (strstr(pStr, "film") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0);

   if (strstr(pStr, "météo") != NULL)
      XMLTV_ADD_THEME_CODE(0x20, 0x21);
   if (strstr(pStr, "document") != NULL)
      XMLTV_ADD_THEME_CODE(0x20, 0x23);
   if ( (strstr(pStr, "discussion") != NULL) ||
        (strstr(pStr, "interview") != NULL) ||
        (strstr(pStr, "débat") != NULL) )
      XMLTV_ADD_THEME_CODE(0x20, 0x24);
   if (strstr(pStr, "journal") != NULL)
      XMLTV_ADD_THEME_CODE(0x20, 0);

   if (strstr(pStr, "reportage") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x26);
   if (strstr(pStr, "investissement") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x27);
   if (strstr(pStr, "presse people") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x28);
   if ( (strstr(pStr, "économie") != NULL) ||
        (strstr(pStr, "politique") != NULL) ||
        (strstr(pStr, "socio") != NULL) )
      XMLTV_ADD_THEME_CODE(0x25, 0);

   if (strstr(pStr, "talk") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0x33);
   if ( (strstr(pStr, "quiz") != NULL) ||
        (strncmp(pStr, "jeu", 3) == 0) ||
        (strstr(pStr, "concours") != NULL) )
      XMLTV_ADD_THEME_CODE(0x30, 0x31);
   if (strstr(pStr, "variété") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0x32);
   if (strstr(pStr, "show") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0);

   if ( (strstr(pStr, "voyage") != NULL) ||
        (strstr(pStr, "tourisme") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x35);
   if (strstr(pStr, "artisanat") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x36);
   if (strstr(pStr, "automobile") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x37);
   if ( (strstr(pStr, "fitness") != NULL) ||
        (strstr(pStr, "santé") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x38);
   if (strstr(pStr, "cuisine") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x39);
   if (strstr(pStr, "achat") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x3a);
   if (strstr(pStr, "loisirs") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0);

   if (strstr(pStr, "football") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x43);
   if ( (strstr(pStr, "tennis") != NULL) ||
        (strstr(pStr, "squash") != NULL) )
      XMLTV_ADD_THEME_CODE(0x40, 0x44);
   if (strstr(pStr, "athlétisme") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x46);
   if (strstr(pStr, "sports") != NULL)
   {
      if (strstr(pStr, "mécaniques") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x47);
      if (strstr(pStr, "aquatiques") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x48);
      if (strstr(pStr, "d'hiver") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x49);
      if (strstr(pStr, "locaux") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x4c);
      if (strstr(pStr, "événements") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x41);
   }
   if (strstr(pStr, "équitation") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x4a);
   if (strstr(pStr, "martiaux") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x4b);
   if (strstr(pStr, "magazine sportif") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x42);
   if (strstr(pStr, "volley") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x45);
   if ( (strstr(pStr, "motocyclisme") != NULL) ||
        (strstr(pStr, "formule 1") != NULL) )
      XMLTV_ADD_THEME_CODE(0x40, 0x47);
   if ( (strstr(pStr, "cyclisme") != NULL) ||
        (strstr(pStr, "golf") != NULL) ||
        (strncmp(pStr, "boxe", 4) == 0) ||
        (strncmp(pStr, "ski", 3) == 0) ||
        (strstr(pStr, "sport") != NULL) )
      XMLTV_ADD_THEME_CODE(0x40, 0);

   if ( (strstr(pStr, "marionnette") != NULL) ||
        (strstr(pStr, "dessin animé") != NULL) )
      XMLTV_ADD_THEME_CODE(0x50, 0x55);
   if (strstr(pStr, "jeunes") != NULL)
   {
      if (strstr(pStr, "enseignement") != NULL)
         XMLTV_ADD_THEME_CODE(0x50, 0x54);
      XMLTV_ADD_THEME_CODE(0x50, 0);
   }

   if (strstr(pStr, "nature") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x57);
   if (strstr(pStr, "technologie") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x58);
   if (strstr(pStr, "médecine") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x59);
   if (strstr(pStr, "étranger") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5a);
   if (strstr(pStr, "éducation") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5c);
   if (strstr(pStr, "langues") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5d);
   if (strstr(pStr, "science") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0);

   if (strstr(pStr, "rock") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x61);
   if (strstr(pStr, "jazz") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x64);
   if ( (strstr(pStr, "opéra") != NULL) ||
        (strcmp(pStr, "musical") == 0) ||
        (strstr(pStr, "comédie musicale") != NULL) )
      XMLTV_ADD_THEME_CODE(0x60, 0x65);
   if (strstr(pStr, "ballett") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x66);
   if (strstr(pStr, "musique") != NULL)
   {
      if (strstr(pStr, "classique") != NULL)
         XMLTV_ADD_THEME_CODE(0x60, 0x62);
      if ( (strstr(pStr, "folk") != NULL) ||
           (strstr(pStr, "traditionnelle") != NULL) )
         XMLTV_ADD_THEME_CODE(0x60, 0x63);
      XMLTV_ADD_THEME_CODE(0x60, 0);
   }

   if (strstr(pStr, "religion") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x73);
   if (strcmp(pStr, "représentations artistiques") == 0)
      XMLTV_ADD_THEME_CODE(0x70, 0x71);
   if (strstr(pStr, "beaux arts") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x72);
   if (strstr(pStr, "culture pop") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x74);
   if (strstr(pStr, "littérature") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x75);
   if (strstr(pStr, "cinéma") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x76);
   if ( (strstr(pStr, "amateur") != NULL) &&
        ( (strstr(pStr, "film") != NULL) ||
          (strstr(pStr, "video") != NULL) ))
      XMLTV_ADD_THEME_CODE(0x70, 0x77);
   if (strstr(pStr, "presse") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x78);
   if (strncmp(pStr, "mode", 4) == 0)
      XMLTV_ADD_THEME_CODE(0x70, 0x7b);
   if ( (strstr(pStr, "arts") != NULL) ||
        (strstr(pStr, "culture") != NULL) )
      XMLTV_ADD_THEME_CODE(0x70, 0);

   dprintf1("Theme-French: '%s': unknown\n", pStr);
   pCache->cat = 0;
   pCache->theme = 0;
   return;

done:
   dprintf3("Theme-French: '%s': 0x%02x,%02x\n", pStr, pCache->cat, pCache->theme);
   return;
}

// ----------------------------------------------------------------------------
// British theme names
//
void Xmltv_ParseThemeStringEnglish( HASHED_THEMES * pCache, const char * pStr )
{
   if ( (strstr(pStr, "series") != NULL) ||
        (strstr(pStr, "sitcom") != NULL) )
      XMLTV_ADD_THEME_CODE(0x80, 0);

   if ( (strstr(pStr, "detective") != NULL) ||
        (strstr(pStr, "thriller") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x11);
   if ( (strncmp(pStr, "war", 3) == 0) ||
        (strstr(pStr, "western") != NULL) ||
        (strstr(pStr, "adventure") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x12);
   if ( (strstr(pStr, "fiction") != NULL) ||
        (strstr(pStr, "sci-fi") != NULL) ||
        (strstr(pStr, "science fiction") != NULL) ||
        (strstr(pStr, "fantasy") != NULL) ||
        (strstr(pStr, "horror") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x13);
   if (strstr(pStr, "comedy") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0x14);
   if ( (strstr(pStr, "drama") != NULL) ||
        (strstr(pStr, "folklore") != NULL) ||
        (strstr(pStr, "soap") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x15);
   if ( (strstr(pStr, "romanti") != NULL) ||
        (strstr(pStr, "romance") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x16);
   if ( (strstr(pStr, "histor") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x17);
   if ( (strstr(pStr, "erotic") != NULL) ||
        (strstr(pStr, "adult") != NULL) )
      XMLTV_ADD_THEME_CODE(0x10, 0x18);
   if (strstr(pStr, "movie") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0);

   if (strstr(pStr, "weather") != NULL)
      XMLTV_ADD_THEME_CODE(0x20, 0x21);
   if (strstr(pStr, "document") != NULL)
      XMLTV_ADD_THEME_CODE(0x20, 0x23);
   if ( (strstr(pStr, "discussion") != NULL) ||
        (strstr(pStr, "interview") != NULL) ||
        (strstr(pStr, "debate") != NULL) )
      XMLTV_ADD_THEME_CODE(0x20, 0x24);
   if ( (strstr(pStr, "current events") != NULL) ||
        (strstr(pStr, "current affairs") != NULL) ||
        (strstr(pStr, "news") != NULL) )
      XMLTV_ADD_THEME_CODE(0x20, 0);

   if (strstr(pStr, "report") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x26);
   if ( (strstr(pStr, "advisory") != NULL) ||
        (strstr(pStr, "business") != NULL) )
      XMLTV_ADD_THEME_CODE(0x25, 0x27);
   if (strstr(pStr, "remarkable people") != NULL)
      XMLTV_ADD_THEME_CODE(0x25, 0x28);
   if ( (strstr(pStr, "economics") != NULL) ||
        (strstr(pStr, "politi") != NULL) ||
        (strstr(pStr, "social") != NULL) )
      XMLTV_ADD_THEME_CODE(0x25, 0);

   if (strstr(pStr, "talk") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0x33);
   if ( (strstr(pStr, "quiz") != NULL) ||
        (strstr(pStr, "contest") != NULL) )
      XMLTV_ADD_THEME_CODE(0x30, 0x31);
   if (strstr(pStr, "variety show") != NULL)
      XMLTV_ADD_THEME_CODE(0x30, 0x32);
   if ( (strstr(pStr, "game") != NULL) ||
        (strstr(pStr, "show") != NULL) )
      XMLTV_ADD_THEME_CODE(0x30, 0);

   if ( (strstr(pStr, "travel") != NULL) ||
        (strstr(pStr, "tourism") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x35);
   if (strstr(pStr, "handicraft") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x36);
   if (strstr(pStr, "motoring") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x37);
   if ( (strstr(pStr, "fitness") != NULL) ||
        (strstr(pStr, "health") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x38);
   if (strstr(pStr, "cooking") != NULL)
      XMLTV_ADD_THEME_CODE(0x34, 0x39);
   if ( (strstr(pStr, "shopping") != NULL) ||
        (strstr(pStr, "advertisement") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0x3a);
   if ( (strstr(pStr, "leisure") != NULL) ||
        (strstr(pStr, "interests") != NULL) ||
        (strstr(pStr, "hobbies") != NULL) ||
        (strstr(pStr, "gardening") != NULL) )
      XMLTV_ADD_THEME_CODE(0x34, 0);

   if (strstr(pStr, "football") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x43);
   if ( (strstr(pStr, "tennis") != NULL) ||
        (strstr(pStr, "squash") != NULL) )
      XMLTV_ADD_THEME_CODE(0x40, 0x44);
   if (strstr(pStr, "athletics") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x46);
   if (strstr(pStr, "equestrian") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x4a);
   if (strstr(pStr, "martial") != NULL)
      XMLTV_ADD_THEME_CODE(0x40, 0x4b);
   if (strstr(pStr, "sport") != NULL)
   {
      if (strstr(pStr, "motor") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x47);
      if (strstr(pStr, "water") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x48);
      if (strstr(pStr, "winter") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x49);
      if (strstr(pStr, "magazine") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x42);
      if (strstr(pStr, "events") != NULL)
         XMLTV_ADD_THEME_CODE(0x40, 0x41);
      XMLTV_ADD_THEME_CODE(0x40, 0);
   }
   if ( (strstr(pStr, "cycl") != NULL) ||
        (strstr(pStr, "golf") != NULL) ||
        (strncmp(pStr, "box", 4) == 0) ||
        (strncmp(pStr, "ski", 3) == 0) )
      XMLTV_ADD_THEME_CODE(0x40, 0);

   if (strstr(pStr, "pre-school") != NULL)
      XMLTV_ADD_THEME_CODE(0x50, 0x51);
   if ( (strstr(pStr, "cartoons") != NULL) ||
        (strstr(pStr, "animation") != NULL) ||
        (strstr(pStr, "puppets") != NULL) )
      XMLTV_ADD_THEME_CODE(0x50, 0x55);
   if ( (strstr(pStr, "kids") != NULL) ||
        (strstr(pStr, "children") != NULL) )
   {
      if (strstr(pStr, "education") != NULL)
         XMLTV_ADD_THEME_CODE(0x50, 0x54);
      XMLTV_ADD_THEME_CODE(0x50, 0);
   }

   if ( (strstr(pStr, "environment") != NULL) ||
        (strstr(pStr, "nature") != NULL) )
      XMLTV_ADD_THEME_CODE(0x56, 0x57);
   if (strstr(pStr, "technology") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x58);
   if (strstr(pStr, "medicine") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x59);
   if (strstr(pStr, "foreign") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5a);
   if (strstr(pStr, "education") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5c);
   if (strstr(pStr, "language") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0x5d);
   if (strstr(pStr, "science") != NULL)
      XMLTV_ADD_THEME_CODE(0x56, 0);

   if (strstr(pStr, "rock") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x61);
   if (strstr(pStr, "jazz") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x64);
   if (strstr(pStr, "opera") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x65);
   if (strstr(pStr, "ballet") != NULL)
      XMLTV_ADD_THEME_CODE(0x60, 0x66);
   if (strstr(pStr, "music") != NULL)
   {
      if (strstr(pStr, "classical") != NULL)
         XMLTV_ADD_THEME_CODE(0x60, 0x62);
      if ( (strstr(pStr, "folk") != NULL) ||
           (strstr(pStr, "traditional") != NULL) )
         XMLTV_ADD_THEME_CODE(0x60, 0x63);
      if (strstr(pStr, "musical") != NULL)
         XMLTV_ADD_THEME_CODE(0x60, 0x65);
      XMLTV_ADD_THEME_CODE(0x60, 0);
   }

   if (strstr(pStr, "performing arts") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x71);
   if (strstr(pStr, "religion") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x73);
   if (strstr(pStr, "literature") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x75);
   if (strstr(pStr, "cinema") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x76);
   if ( (strstr(pStr, "experiment") != NULL) &&
        ( (strstr(pStr, "film") != NULL) ||
          (strstr(pStr, "video") != NULL) ))
      XMLTV_ADD_THEME_CODE(0x70, 0x77);
   if ( (strstr(pStr, "press") != NULL) ||
        (strstr(pStr, "broadcasting") != NULL) )
      XMLTV_ADD_THEME_CODE(0x70, 0x78);
   if (strstr(pStr, "fashion") != NULL)
      XMLTV_ADD_THEME_CODE(0x70, 0x7b);
   if ( (strstr(pStr, "arts") != NULL) ||
        (strstr(pStr, "culture") != NULL) )
   {
      if (strstr(pStr, "fine") != NULL)
         XMLTV_ADD_THEME_CODE(0x70, 0x72);
      if (strstr(pStr, "pop") != NULL)
         XMLTV_ADD_THEME_CODE(0x70, 0x74);
      if (strstr(pStr, "magazine") != NULL)
         XMLTV_ADD_THEME_CODE(0x70, 0x7a);
      XMLTV_ADD_THEME_CODE(0x70, 0);
   }

   if (strstr(pStr, "film") != NULL)
      XMLTV_ADD_THEME_CODE(0x10, 0);

   dprintf1("Theme-English: '%s': unknown\n", pStr);
   pCache->cat = 0;
   pCache->theme = 0;
   return;

done:
   dprintf3("Theme-English: '%s': 0x%02x,%02x\n", pStr, pCache->cat, pCache->theme);
   return;
}

