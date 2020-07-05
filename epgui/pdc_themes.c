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
 *  $Id: pdc_themes.c,v 1.13 2020/06/17 19:32:20 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_EPGUI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgui/pdc_themes.h"

static const char * const pdc_themes_eng[] =
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

#ifdef USE_UTF8
static const char * const pdc_themes_ger[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "Spielfilm",
   /*0x11*/ "Krimi/Thriller",
   /*0x12*/ "Abenteuer/Western/Krieg",
   /*0x13*/ "Sci-Fi/Fantasy/Horror",
   /*0x14*/ "KomÃ¶die",
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
   /*0x28*/ "PersÃ¶nlichkeiten",
            0,0,0,0,0,0,0,
   /*0x30*/ "Show/Gameshow",
   /*0x31*/ "Quiz/Spiele/Wettstreit",
   /*0x32*/ "VarietÃ©",
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
   /*0x41*/ "Sportveranstaltungen",
   /*0x42*/ "Sportmagazin",
   /*0x43*/ "FuÃŸball",
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
   /*0x52*/ "FÃ¼r 6 bis 14 jÃ¤hrige",
   /*0x53*/ "FÃ¼r 10 bis 16 jÃ¤hrige",
   /*0x54*/ "Bildung fÃ¼r Kinder",
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
   /*0x71*/ "Darstellende KÃ¼nste",
   /*0x72*/ "Hohe KÃ¼nste",
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
#else // !USE_UTF8
static const char * const pdc_themes_ger[] =
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
   /*0x41*/ "Sportveranstaltungen",
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
#endif // !USE_UTF8

#ifdef USE_UTF8
static const char * const pdc_themes_fra[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "Film",
   /*0x11*/ "Policier/Thriller",
   /*0x12*/ "Aventure/Western/Guerre",
   /*0x13*/ "Science-Fiction/Fantastique/Horreur",
   /*0x14*/ "ComÃ©die",
   /*0x15*/ "MÃ©lodrame/Folklore",
   /*0x16*/ "Romance",
   /*0x17*/ "Drame Historique",
   /*0x18*/ "Film pour Adulte",
            0,0,0,0,0,0,0,
   /*0x20*/ "Journal",
   /*0x21*/ "Journal/MÃ©tÃ©o",
   /*0x22*/ "Magazine d'Information",
   /*0x23*/ "Documentaire",
   /*0x24*/ "Discussion/Interview/DÃ©bat",

   /*0x25*/ "Socio/Ã‰conomie/Politique",
   /*0x26*/ "Magazine Info & Reportage",
   /*0x27*/ "Bourse et Investissement",
   /*0x28*/ "Presse People",
            0,0,0,0,0,0,0,
   /*0x30*/ "Spectacle/Jeu",
   /*0x31*/ "Jeu/Quiz/Concours",
   /*0x32*/ "Ã‰mission de VariÃ©tÃ©",
   /*0x33*/ "Talk show",

   /*0x34*/ "Loisirs",
   /*0x35*/ "Tourisme/Voyage",
   /*0x36*/ "Artisanat",
   /*0x37*/ "Automobile",
   /*0x38*/ "SantÃ©",
   /*0x39*/ "Cuisine",
   /*0x3A*/ "TÃ©lÃ© achat",
            0,0,0,0,0,
   /*0x40*/ "Sports",
   /*0x41*/ "Ã‰vÃ©nements Particuliers",
   /*0x42*/ "Magazine sportif",
   /*0x43*/ "Football",
   /*0x44*/ "Tennis & Squash",
   /*0x45*/ "Autres Sports collectifs",
   /*0x46*/ "AthlÃ©tisme",
   /*0x47*/ "Sports mÃ©caniques",
   /*0x48*/ "Sports aquatiques",
   /*0x49*/ "Sports d'hiver",
   /*0x4A*/ "Ã‰quitation",
   /*0x4B*/ "Arts martiaux",
   /*0x4C*/ "Sports locaux",
            0,0,0,
   /*0x50*/ "Jeunesse",
   /*0x51*/ "Moins de 6 ans",
   /*0x52*/ "Jeunes de 6 Ã  10 ans",
   /*0x53*/ "Jeunes de 10 Ã  16 ans",
   /*0x54*/ "Enseignement pour la Jeunesse",
   /*0x55*/ "Dessin animÃ© & Marionnette",

   /*0x56*/ "Science",
   /*0x57*/ "Nature",
   /*0x58*/ "Technologie",
   /*0x59*/ "MÃ©decine",
   /*0x5A*/ "Ã‰tranger",
   /*0x5B*/ "Social",
   /*0x5C*/ "Ã‰ducation/divers",
   /*0x5D*/ "Langues",
            0,0,
   /*0x60*/ "Musique/Ballet/Danse",
   /*0x61*/ "Rock & Pop",
   /*0x62*/ "Musique classique/divers",
   /*0x63*/ "Folk & Musique traditionnelle",
   /*0x64*/ "Jazz",
   /*0x65*/ "ComÃ©die musicale & OpÃ©ra",
   /*0x66*/ "Ballet",
            0,0,0,0,0,0,0,0,0,
   /*0x70*/ "Arts/Culture",
   /*0x71*/ "ReprÃ©sentations artistiques",
   /*0x72*/ "Beaux arts",
   /*0x73*/ "Religion",
   /*0x74*/ "Culture Pop & Arts traditionnels",
   /*0x75*/ "LittÃ©rature",
   /*0x76*/ "Film & CinÃ©ma",
   /*0x77*/ "Film/VidÃ©o amateur",
   /*0x78*/ "TÃ©lÃ©vision & Presse",
   /*0x79*/ "Nouveau Media",
   /*0x7A*/ "Arts & Magazines culturels",
   /*0x7B*/ "Mode",
            0,0,0,0,
   /*0x80*/ "SÃ©ries",
};
#else // !USE_UTF8
static const char * const pdc_themes_fra[] =
{
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   /*0x10*/ "Film",
   /*0x11*/ "Policier/Thriller",
   /*0x12*/ "Aventure/Western/Guerre",
   /*0x13*/ "Science-Fiction/Fantastique/Horreur",
   /*0x14*/ "Comédie",
   /*0x15*/ "Mélodrame/Folklore",
   /*0x16*/ "Romance",
   /*0x17*/ "Drame Historique",
   /*0x18*/ "Film pour Adulte",
            0,0,0,0,0,0,0,
   /*0x20*/ "Journal",
   /*0x21*/ "Journal/Météo",
   /*0x22*/ "Magazine d'Information",
   /*0x23*/ "Documentaire",
   /*0x24*/ "Discussion/Interview/Débat",

   /*0x25*/ "Socio/Économie/Politique",
   /*0x26*/ "Magazine Info & Reportage",
   /*0x27*/ "Bourse et Investissement",
   /*0x28*/ "Presse People",
            0,0,0,0,0,0,0,
   /*0x30*/ "Spectacle/Jeu",
   /*0x31*/ "Jeu/Quiz/Concours",
   /*0x32*/ "Émission de Variété",
   /*0x33*/ "Talk show",

   /*0x34*/ "Loisirs",
   /*0x35*/ "Tourisme/Voyage",
   /*0x36*/ "Artisanat",
   /*0x37*/ "Automobile",
   /*0x38*/ "Santé",
   /*0x39*/ "Cuisine",
   /*0x3A*/ "Télé achat",
            0,0,0,0,0,
   /*0x40*/ "Sports",
   /*0x41*/ "Événements Particuliers",
   /*0x42*/ "Magazine sportif",
   /*0x43*/ "Football",
   /*0x44*/ "Tennis & Squash",
   /*0x45*/ "Autres Sports collectifs",
   /*0x46*/ "Athlétisme",
   /*0x47*/ "Sports mécaniques",
   /*0x48*/ "Sports aquatiques",
   /*0x49*/ "Sports d'hiver",
   /*0x4A*/ "Équitation",
   /*0x4B*/ "Arts martiaux",
   /*0x4C*/ "Sports locaux",
            0,0,0,
   /*0x50*/ "Jeunesse",
   /*0x51*/ "Moins de 6 ans",
   /*0x52*/ "Jeunes de 6 à 10 ans",
   /*0x53*/ "Jeunes de 10 à 16 ans",
   /*0x54*/ "Enseignement pour la Jeunesse",
   /*0x55*/ "Dessin animé & Marionnette",

   /*0x56*/ "Science",
   /*0x57*/ "Nature",
   /*0x58*/ "Technologie",
   /*0x59*/ "Médecine",
   /*0x5A*/ "Étranger",
   /*0x5B*/ "Social",
   /*0x5C*/ "Éducation/divers",
   /*0x5D*/ "Langues",
            0,0,
   /*0x60*/ "Musique/Ballet/Danse",
   /*0x61*/ "Rock & Pop",
   /*0x62*/ "Musique classique/divers",
   /*0x63*/ "Folk & Musique traditionnelle",
   /*0x64*/ "Jazz",
   /*0x65*/ "Comédie musicale & Opéra",
   /*0x66*/ "Ballet",
            0,0,0,0,0,0,0,0,0,
   /*0x70*/ "Arts/Culture",
   /*0x71*/ "Représentations artistiques",
   /*0x72*/ "Beaux arts",
   /*0x73*/ "Religion",
   /*0x74*/ "Culture Pop & Arts traditionnels",
   /*0x75*/ "Littérature",
   /*0x76*/ "Film & Cinéma",
   /*0x77*/ "Film/Vidéo amateur",
   /*0x78*/ "Télévision & Presse",
   /*0x79*/ "Nouveau Media",
   /*0x7A*/ "Arts & Magazines culturels",
   /*0x7B*/ "Mode",
            0,0,0,0,
   /*0x80*/ "Séries",
};
#endif // !USE_UTF8

static const char * const pdc_general_eng = " - general";
static const char * const pdc_general_ger = " - allgemein";
static const char * const pdc_general_fra = " - général";

static const char * const pdc_undefined_eng = "undefined";
static const char * const pdc_undefined_ger = "unbekannt";
static const char * const pdc_undefined_fra = "non défini";

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

static const char * const * pdc_themes = pdc_themes_eng;
static const char * pdc_general        = "";
static const char * pdc_undefined      = "";

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
const char * PdcThemeGet( uchar theme )
{
   if (theme <= PDC_THEME_LAST)
      return pdc_themes[theme];
   else
      return pdc_undefined;
}

// ---------------------------------------------------------------------------
// Returns predefined text for the given PDC theme index or NULL
//
const char * PdcThemeGetByLang( uchar theme, uchar lang )
{
   const char * pResult;

   if (theme <= PDC_THEME_LAST)
   {
      switch (lang)
      {
         case 1:  pResult = pdc_themes_ger[theme]; break;
         case 4:  pResult = pdc_themes_fra[theme]; break;
         default:
         case 0:  pResult = pdc_themes_eng[theme]; break;
      }
   }
   else
      pResult = pdc_undefined;

   return pResult;
}

// ---------------------------------------------------------------------------
// Returns predefined text for the given PDC theme index
// - if the given index is a category it also returns " - general"
// - if there is no text for the given index NULL is returned, or optionally
//   "undefined"
//
const char * PdcThemeGetWithGeneral( uchar theme, const char ** pGeneralStr, bool withUndef )
{
   const char * pThemeStr;
   bool isGeneral;
   uint idx;

   isGeneral = FALSE;
   if (theme <= PDC_THEME_LAST)
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
      pThemeStr = pdc_undefined;

   if (pGeneralStr != NULL)
      *pGeneralStr = isGeneral ? (char *) pdc_general : "";

   return pThemeStr;
}

// ---------------------------------------------------------------------------
// Check if the given PDC theme index is predefined by ETSI
//
bool PdcThemeIsDefined( uchar theme )
{
   if (theme <= PDC_THEME_LAST)
      return (pdc_themes[theme] != NULL);
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// Determine into which category the given theme falls
//
uchar PdcThemeGetCategory( uchar theme )
{
   uint  idx;
   uint  category;

   if (theme < PDC_THEME_LAST)
   {
      idx = (theme >> (4 - 1)) & 0x0e;

      if (theme >= pdc_categories[idx + 1])
         category = pdc_categories[idx + 1];
      else
         category = pdc_categories[idx];
   }
   else
      category = PDC_THEME_LAST;

   return category;
}
