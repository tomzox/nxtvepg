/*
 *  PDC theme names definition
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
 *    This table conforms to the programme type principle of classification
 *    as defined in in ETS 300 231 (PDC) and ETS 300 707 (Nextview EPG)
 *    See http://www.etsi.org/
 *
 *
 *  Author: Tom Zoerner
 *
 *    French translation by mat (mat100@ifrance.com) with help of nash bridges.
 *
 *  $Id: pdc_themes.c,v 1.8 2002/07/20 16:28:05 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgui/pdc_themes.h"

static const uchar * const pdc_themes_eng[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "movie",
   /*0x11*/ "detective/thriller",
   /*0x12*/ "adventure/western/war",
   /*0x13*/ "sci-fi/fantasy/horror",
   /*0x14*/ "comedy",
   /*0x15*/ "melodrama/folklore",
   /*0x16*/ "romance",
   /*0x17*/ "historical drama",
   /*0x18*/ "adult movie",
            0,0,0,0,0,0,0,
   /*0x20*/ "news",
   /*0x21*/ "news",
   /*0x22*/ "news magazine",
   /*0x23*/ "documentary",
   /*0x24*/ "discussion/interview/debate",

   /*0x25*/ "social/political/economics",
   /*0x26*/ "news magazines/reports",
   /*0x27*/ "economics/social advisory",
   /*0x28*/ "remarkable people",
            0,0,0,0,0,0,0,
   /*0x30*/ "show/game show",
   /*0x31*/ "game/show/quiz/contest",
   /*0x32*/ "variety show",
   /*0x33*/ "talk show",

   /*0x34*/ "leisure hobbies",
   /*0x35*/ "tourism/travel",
   /*0x36*/ "handicraft",
   /*0x37*/ "motoring",
   /*0x38*/ "fitness and health",
   /*0x39*/ "cooking",
   /*0x3A*/ "advertisement/shopping",
            0,0,0,0,0,
   /*0x40*/ "sports",
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
   /*0x50*/ "kids",
   /*0x51*/ "pre-school",
   /*0x52*/ "kids from 6 to 14",
   /*0x53*/ "kids from 10 to 16",
   /*0x54*/ "kids educational",
   /*0x55*/ "cartoons & puppets",

   /*0x56*/ "science",
   /*0x57*/ "nature",
   /*0x58*/ "technology",
   /*0x59*/ "medicine",
   /*0x5A*/ "foreign",
   /*0x5B*/ "social",
   /*0x5C*/ "misc. education",
   /*0x5D*/ "languages",
            0,0,
   /*0x60*/ "music/ballet/dance",
   /*0x61*/ "rock & pop",
   /*0x62*/ "serious & classical music",
   /*0x63*/ "folk & traditional music",
   /*0x64*/ "jazz",
   /*0x65*/ "musical & opera",
   /*0x66*/ "ballet",
            0,0,0,0,0,0,0,0,0,
   /*0x70*/ "arts/culture",
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
   /*0x80*/ "series",
};

static const uchar * const pdc_themes_ger[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "Spielfilm",
   /*0x11*/ "Krimi/Thriller",
   /*0x12*/ "Abenteuer/Western/Krieg",
   /*0x13*/ "Sci-Fi/Fantasy/Horror",
   /*0x14*/ "Komödie",
   /*0x15*/ "Melodrama/Folklore",
   /*0x16*/ "Romantik",
   /*0x17*/ "Historienfilm",
   /*0x18*/ "Erotik",
            0,0,0,0,0,0,0,
   /*0x20*/ "Nachrichten",
   /*0x21*/ "Nachrichten",
   /*0x22*/ "Magazin",
   /*0x23*/ "Dokumentation",
   /*0x24*/ "Diskussion/Interview/Debatte",

   /*0x25*/ "Soziales/Politik/Wirtschaft",
   /*0x26*/ "Nachrichtenmagazin/Berichte",
   /*0x27*/ "Wirtschaft/Ratgeber",
   /*0x28*/ "Persönlichkeiten",
            0,0,0,0,0,0,0,
   /*0x30*/ "Show/Gameshow",
   /*0x31*/ "Quiz/Spiele/Wettstreit",
   /*0x32*/ "Varieté",
   /*0x33*/ "Talkshow",

   /*0x34*/ "Hobbies",
   /*0x35*/ "Reise/Tourismus",
   /*0x36*/ "Handarbeit",
   /*0x37*/ "Auto/Motor",
   /*0x38*/ "Fitness/Gesundheit",
   /*0x39*/ "Kochen",
   /*0x3A*/ "Shopping/Werbeveranstaltungen",
            0,0,0,0,0,
   /*0x40*/ "Sport",
   /*0x41*/ "Sporterveranstaltungen",
   /*0x42*/ "Sportmagazin",
   /*0x43*/ "Fußball",
   /*0x44*/ "Tennis & Squash",
   /*0x45*/ "Teamsportarten",
   /*0x46*/ "Atletik",
   /*0x47*/ "Motorsport",
   /*0x48*/ "Wassersport",
   /*0x49*/ "Wintersport",
   /*0x4A*/ "Pferdesport",
   /*0x4B*/ "Kampfsport",
   /*0x4C*/ "Lokaler Sport",
            0,0,0,
   /*0x50*/ "Kinderprogramm",
   /*0x51*/ "Vorschulprogramm",
   /*0x52*/ "Für 6 bis 14 jährige",
   /*0x53*/ "Für 10 bis 16 jährige",
   /*0x54*/ "Bildung für Kinder",
   /*0x55*/ "Zeichentrick & Puppenspiel",

   /*0x56*/ "Wissenschaft",
   /*0x57*/ "Natur",
   /*0x58*/ "Technologie",
   /*0x59*/ "Medizin",
   /*0x5A*/ "Ausland",
   /*0x5B*/ "Soziales",
   /*0x5C*/ "Fortbildung",
   /*0x5D*/ "Sprachen",
            0,0,
   /*0x60*/ "Musik/Ballett/Tanz",
   /*0x61*/ "Rock & Pop",
   /*0x62*/ "Ernste & klassische Musik",
   /*0x63*/ "Folksmusik & traditionelle Musik",
   /*0x64*/ "Jazz",
   /*0x65*/ "Musical & Oper",
   /*0x66*/ "Ballett",
            0,0,0,0,0,0,0,0,0,
   /*0x70*/ "Kunst/Kultur",
   /*0x71*/ "Darstellende Künste",
   /*0x72*/ "Hohe Künste",
   /*0x73*/ "Religion",
   /*0x74*/ "Popkultur/traditionelle Kunst",
   /*0x75*/ "Literatur",
   /*0x76*/ "Film & Kino",
   /*0x77*/ "Experimentelle Filme/Video",
   /*0x78*/ "TV & Presse",
   /*0x79*/ "Neue Medien",
   /*0x7A*/ "Kunst & Kulturmagazine",
   /*0x7B*/ "Mode",
            0,0,0,0,
   /*0x80*/ "Serie",
};

static const uchar * const pdc_themes_fra[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "film",
   /*0x11*/ "policier/thriller",
   /*0x12*/ "aventure/western/guerre",
   /*0x13*/ "science-fiction/fantastique/horreur",
   /*0x14*/ "comédie",
   /*0x15*/ "mélodrame/folklore",
   /*0x16*/ "romance",
   /*0x17*/ "drame historique",
   /*0x18*/ "film pour adulte",
            0,0,0,0,0,0,0,
   /*0x20*/ "journal",
   /*0x21*/ "journal/météo",
   /*0x22*/ "magazine d'information",
   /*0x23*/ "documentaire",
   /*0x24*/ "discussion/interview/débat",

   /*0x25*/ "socio/économie/politique",
   /*0x26*/ "magazine info & reportage",
   /*0x27*/ "bourse et investissement",
   /*0x28*/ "presse people",
            0,0,0,0,0,0,0,
   /*0x30*/ "spectacle/jeu",
   /*0x31*/ "jeu/quiz/concours",
   /*0x32*/ "émission de variété",
   /*0x33*/ "talk show",

   /*0x34*/ "loisirs",
   /*0x35*/ "tourisme/voyage",
   /*0x36*/ "artisanat",
   /*0x37*/ "automobile",
   /*0x38*/ "santé",
   /*0x39*/ "cuisine",
   /*0x3A*/ "télé achat",
            0,0,0,0,0,
   /*0x40*/ "sports",
   /*0x41*/ "événements particuliers (JO,...)",
   /*0x42*/ "magazine sportif",
   /*0x43*/ "football",
   /*0x44*/ "tennis & squash",
   /*0x45*/ "autres sports collectifs",
   /*0x46*/ "athlétisme",
   /*0x47*/ "sports mécaniques",
   /*0x48*/ "sports aquatiques",
   /*0x49*/ "sports hivernales",
   /*0x4A*/ "équitation",
   /*0x4B*/ "arts martiaux",
   /*0x4C*/ "sports locaux",
            0,0,0,
   /*0x50*/ "jeunesse",
   /*0x51*/ "moins de 6 ans",
   /*0x52*/ "jeunes de 6 à 10 ans",
   /*0x53*/ "jeunes de 10 à 16 ans",
   /*0x54*/ "enseignement pour la jeunesse",
   /*0x55*/ "dessin animé & marionnette",

   /*0x56*/ "science",
   /*0x57*/ "nature",
   /*0x58*/ "technologie",
   /*0x59*/ "médecine",
   /*0x5A*/ "étranger",
   /*0x5B*/ "social",
   /*0x5C*/ "éducation/divers",
   /*0x5D*/ "langues",
            0,0,
   /*0x60*/ "musique/ballet/danse",
   /*0x61*/ "rock & pop",
   /*0x62*/ "musique classique/divers",
   /*0x63*/ "folk & musique traditionnelle",
   /*0x64*/ "jazz",
   /*0x65*/ "comédie musicale & opéra",
   /*0x66*/ "ballet",
            0,0,0,0,0,0,0,0,0,
   /*0x70*/ "arts/culture",
   /*0x71*/ "représentations artistiques",
   /*0x72*/ "beaux arts",
   /*0x73*/ "religion",
   /*0x74*/ "culture pop & arts traditionnels",
   /*0x75*/ "littérature",
   /*0x76*/ "film & cinéma",
   /*0x77*/ "film/vidéo amateur",
   /*0x78*/ "télévision & presse",
   /*0x79*/ "nouveau media",
   /*0x7A*/ "arts & magazines culturels",
   /*0x7B*/ "mode",
            0,0,0,0,
   /*0x80*/ "séries",
};

static const uchar * const pdc_general_eng = " - general";
static const uchar * const pdc_general_ger = " - allgemein";
static const uchar * const pdc_general_fra = " - général";

static const uchar * const pdc_undefined_eng = "undefined";
static const uchar * const pdc_undefined_ger = "unbekannt";
static const uchar * const pdc_undefined_fra = "non défini";

static const uchar pdc_categories[2 * 8] =
{
   0,0,
   0x10, // movie - general
   0x10,
   0x20, // news - general
   0x25, // social/political/economics - general
   0x30, // show/game show - general
   0x34, // leisure hobbies - general
   0x40, // sports - general
   0x40,
   0x50, // kids - general
   0x56, // science - general
   0x60, // music/ballet/dance - general
   0x60,
   0x70, // arts/culture - general
   0x70,
};

static const uchar * const * pdc_themes = pdc_themes_eng;
static const uchar * pdc_general        = "";
static const uchar * pdc_undefined      = "";

// ---------------------------------------------------------------------------
// 
//
void PdcThemeSetLanguage( uchar lang )
{
   switch (lang)
   {
      case 1:
         pdc_themes = pdc_themes_ger;
         pdc_general = pdc_general_ger;
         pdc_undefined = pdc_undefined_ger;
         break;
      case 4:
         pdc_themes = pdc_themes_fra;
         pdc_general = pdc_general_fra;
         pdc_undefined = pdc_undefined_fra;
         break;
      default:
      case 0:
         pdc_themes = pdc_themes_eng;
         pdc_general = pdc_general_eng;
         pdc_undefined = pdc_undefined_eng;
         break;
   }
}

// ---------------------------------------------------------------------------
// Returns predefined text for the given PDC theme index or NULL
//
const uchar * PdcThemeGet( uchar theme )
{
   if (theme < 0x80)
      return pdc_themes[theme];
   else
      return pdc_themes[PDC_THEME_SERIES];
}

// ---------------------------------------------------------------------------
// Returns predefined text for the given PDC theme index or NULL
//
const uchar * PdcThemeGetByLang( uchar theme, uchar lang )
{
   const uchar * pResult;

   if (theme >= 0x80)
      theme = PDC_THEME_SERIES;

   switch (lang)
   {
      case 1:  pResult = pdc_themes_ger[theme]; break;
      case 4:  pResult = pdc_themes_fra[theme]; break;
      default:
      case 0:  pResult = pdc_themes_eng[theme]; break;
   }

   return pResult;
}

// ---------------------------------------------------------------------------
// Returns predefined text for the given PDC theme index
// - if the given index is a category it also returns " - general"
// - if there is no text for the given index NULL is returned, or optionally
//   "undefined"
//
const uchar * PdcThemeGetWithGeneral( uchar theme, const uchar ** pGeneralStr, bool withUndef )
{
   const uchar * pThemeStr;
   bool isGeneral;
   uint idx;

   isGeneral = FALSE;
   if (theme < 0x80)
   {
      pThemeStr = pdc_themes[theme];
      if (pThemeStr != NULL)
      {
         idx = (theme >> (4 - 1)) & 0x0e;
         if ( (theme == pdc_categories[idx]) ||
              (theme == pdc_categories[idx + 1]) )
         {
            isGeneral = TRUE;
         }
      }
      else if (withUndef)
      {
         pThemeStr = pdc_undefined;
      }
   }
   else
      pThemeStr = pdc_themes[PDC_THEME_SERIES];

   if (pGeneralStr != NULL)
      *pGeneralStr = isGeneral ? (char *) pdc_general : "";

   return pThemeStr;
}

// ---------------------------------------------------------------------------
// Check if the given PDC theme index is predefined by ETSI
// - returns FALSE for series codes because these are not predefined;
//   their meaning varies between providers
//
bool PdcThemeIsDefined( uchar theme )
{
   if (theme < 0x80)
      return (pdc_themes[theme] != NULL);
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Determine into which category the given theme falls
// - The codes above 0x80 are defined indiviually for each network
//   The names for these codes are implied by the titles of the
//   assigned programme entries, i.e. PI blocks.
//
uchar PdcThemeGetCategory( uchar theme )
{
   uint  idx;
   uint  category;

   if (theme < 0x80)
   {
      idx = (theme >> (4 - 1)) & 0x0e;

      if (theme >= pdc_categories[idx + 1])
         category = pdc_categories[idx + 1];
      else
         category = pdc_categories[idx];
   }
   else
      category = 0x80;

   return category;
}

