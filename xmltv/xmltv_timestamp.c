/**
 * Copyright (c) 2003 Billy Biggs <vektor@dumbterm.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
** Note: optimized, added DTD-0.6 support and error detection [TZO]
*/

#define DEBUG_SWITCH DEBUG_SWITCH_XMLTV
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xmltv/xmltv_timestamp.h"

/* use perfect hash generated by GPERF for timezone name lookup */
#define USE_GPERF 1

/* fast implementation aof atoi; characters have been previously checked to be digits */
#define ATOI_2(S)  (((S)[0]-'0')*10 + ((S)[1]-'0'))
#define ATOI_4(S)  (ATOI_2(S)*100 + ATOI_2(S + 2))

#define DATE_OFF_YEAR    0
#define DATE_OFF_MON     4
#define DATE_OFF_DAY     6
#define DATE_OFF_HOUR    8
#define DATE_OFF_MIN     10
#define DATE_OFF_SEC     12


// when cross-compiling for WIN32 on Linux "timezone" is undefined
#if !defined(__NetBSD__) && !defined(__FreeBSD__)
# if defined(WIN32) && !defined(timezone)
#  define timezone _timezone
# endif
#endif

/**
 * Support the Date::Manip timezone names.  This code will hopefully
 * go away when all XMLTV providers drop these names.  Using names
 * is a bad idea since there is no unified standard for them, and the
 * XMLTV DTD does not define a set of standard names to use.
 */
typedef struct tz_map_s {
    const char *name;
    int offset;
} tz_map_t;

#define HM2SEC(OFF) (60 * (((OFF) % 100) + (((OFF) / 100) * 60)))

#ifndef USE_GPERF
static const tz_map_t date_manip_timezones[] = {
    { "IDLW",    -HM2SEC(1200), }, { "NT",      -HM2SEC(1100), }, { "HST",     -HM2SEC(1000), },
    { "CAT",     -HM2SEC(1000), }, { "AHST",    -HM2SEC(1000), }, { "AKST",     -HM2SEC(900), },
    { "YST",      -HM2SEC(900), }, { "HDT",      -HM2SEC(900), }, { "AKDT",     -HM2SEC(800), },
    { "YDT",      -HM2SEC(800), }, { "PST",      -HM2SEC(800), }, { "PDT",      -HM2SEC(700), },
    { "MST",      -HM2SEC(700), }, { "MDT",      -HM2SEC(600), }, { "CST",      -HM2SEC(600), },
    { "CDT",      -HM2SEC(500), }, { "EST",      -HM2SEC(500), }, { "ACT",      -HM2SEC(500), },
    { "SAT",      -HM2SEC(400), }, { "BOT",      -HM2SEC(400), }, { "EDT",      -HM2SEC(400), },
    { "AST",      -HM2SEC(400), }, { "AMT",      -HM2SEC(400), }, { "ACST",     -HM2SEC(400), },
    { "NFT",      -HM2SEC(330), }, { "BRST",     -HM2SEC(300), }, { "BRT",      -HM2SEC(300), },
    { "AMST",     -HM2SEC(300), }, { "ADT",      -HM2SEC(300), }, { "ART",      -HM2SEC(300), },
    { "NDT",      -HM2SEC(230), }, { "AT",       -HM2SEC(200), }, { "BRST",     -HM2SEC(200), },
    { "FNT",      -HM2SEC(200), }, { "WAT",      -HM2SEC(100), }, { "FNST",     -HM2SEC(100), },
    { "GMT",         HM2SEC(0), }, { "UT",          HM2SEC(0), }, { "UTC",         HM2SEC(0), },
    { "WET",         HM2SEC(0), }, { "CET",       HM2SEC(100), }, { "FWT",       HM2SEC(100), },
    { "MET",       HM2SEC(100), }, { "MEZ",       HM2SEC(100), }, { "MEWT",      HM2SEC(100), },
    { "SWT",       HM2SEC(100), }, { "BST",       HM2SEC(100), }, { "GB",        HM2SEC(100), },
    { "WEST",        HM2SEC(0), }, { "CEST",      HM2SEC(200), }, { "EET",       HM2SEC(200), },
    { "FST",       HM2SEC(200), }, { "MEST",      HM2SEC(200), }, { "MESZ",      HM2SEC(200), },
    { "METDST",    HM2SEC(200), }, { "SAST",      HM2SEC(200), }, { "SST",       HM2SEC(200), },
    { "EEST",      HM2SEC(300), }, { "BT",        HM2SEC(300), }, { "MSK",       HM2SEC(300), },
    { "EAT",       HM2SEC(300), }, { "IT",        HM2SEC(330), }, { "ZP4",       HM2SEC(400), },
    { "MSD",       HM2SEC(300), }, { "ZP5",       HM2SEC(500), }, { "IST",       HM2SEC(530), },
    { "ZP6",       HM2SEC(600), }, { "NOVST",     HM2SEC(600), }, { "NST",       HM2SEC(630), },
    { "JAVT",      HM2SEC(700), }, { "CCT",       HM2SEC(800), }, { "AWST",      HM2SEC(800), },
    { "WST",       HM2SEC(800), }, { "PHT",       HM2SEC(800), }, { "JST",       HM2SEC(900), },
    { "ROK",       HM2SEC(900), }, { "ACST",      HM2SEC(930), }, { "CAST",      HM2SEC(930), },
    { "AEST",     HM2SEC(1000), }, { "EAST",     HM2SEC(1000), }, { "GST",      HM2SEC(1000), },
    { "ACDT",     HM2SEC(1030), }, { "CADT",     HM2SEC(1030), }, { "AEDT",     HM2SEC(1100), },
    { "EADT",     HM2SEC(1100), }, { "IDLE",     HM2SEC(1200), }, { "NZST",     HM2SEC(1200), },
    { "NZT",      HM2SEC(1200), }, { "NZDT",     HM2SEC(1300), } };

#define num_timezones (sizeof( date_manip_timezones ) / sizeof( tz_map_t ))
#else  /* USE_GPERF */
/* ANSI-C code produced by gperf version 3.0.1 */
/* Command-line: gperf -tname --ignore-case -LANSI-C -C -G -T */
/* Computed positions: -k'1-4' */

#define TOTAL_KEYWORDS 87
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 6
#define MIN_HASH_VALUE 0
#define MAX_HASH_VALUE 265
/* maximum key range = 266, duplicates = 0 */

static const tz_map_t date_manip_tz_wordlist[] =
  {
    {"ZP6",       HM2SEC(600)},
    {""}, {""}, {""}, {""},
    {"ZP5",       HM2SEC(500)},
    {""}, {""}, {""}, {""},
    {"ZP4",       HM2SEC(400)},
    {""},
    {"IT",        HM2SEC(330)},
    {""},
    {"NT",      -HM2SEC(1100)},
    {""}, {""},
    {"IST",       HM2SEC(530)},
    {""},
    {"NST",       HM2SEC(630)},
    {""}, {""},
    {"IDLW",    -HM2SEC(1200)},
    {""},
    {"NDT",      -HM2SEC(230)},
    {""},
    {"SWT",       HM2SEC(100)},
    {"CST",      -HM2SEC(600)},
    {""},
    {"SST",       HM2SEC(200)},
    {""}, {""},
    {"CDT",      -HM2SEC(500)},
    {""}, {""}, {""},
    {"NOVST",     HM2SEC(600)},
    {"AT",       -HM2SEC(200)},
    {""},
    {"UT",          HM2SEC(0)},
    {""},
    {"GST",      HM2SEC(1000)},
    {"AST",      -HM2SEC(400)},
    {""},
    {"AWST",      HM2SEC(800)},
    {""},
    {"HST",     -HM2SEC(1000)},
    {"ADT",      -HM2SEC(300)},
    {""},
    {"GB",        HM2SEC(100)},
    {""},
    {"HDT",      -HM2SEC(900)},
    {"BT",        HM2SEC(300)},
    {""},
    {"BOT",      -HM2SEC(400)},
    {""},
    {"ART",      -HM2SEC(300)},
    {"BST",       HM2SEC(100)},
    {""}, {""}, {""}, {""},
    {"CCT",       HM2SEC(800)},
    {""},
    {"NFT",      -HM2SEC(330)},
    {""},
    {"AHST",    -HM2SEC(1000)},
    {"EST",      -HM2SEC(500)},
    {""},
    {"WST",       HM2SEC(800)},
    {""},
    {"BRT",      -HM2SEC(300)},
    {"EDT",      -HM2SEC(400)},
    {""},
    {"ROK",       HM2SEC(900)},
    {""},
    {"BRST",     -HM2SEC(300)},
    {"ACT",      -HM2SEC(500)},
    {""},
    {"UTC",         HM2SEC(0)},
    {""}, {""},
    {"ACST",     -HM2SEC(400)},
    {""},
    {"JST",       HM2SEC(900)},
    {""}, {""},
    {"ACDT",     HM2SEC(1030)},
    {""}, {""},
    {"IDLE",     HM2SEC(1200)},
    {"FWT",       HM2SEC(100)},
    {"CET",       HM2SEC(100)},
    {""},
    {"FST",       HM2SEC(200)},
    {""}, {""},
    {"CEST",      HM2SEC(200)},
    {""},
    {"PST",      -HM2SEC(800)},
    {"MSD",       HM2SEC(300)},
    {""},
    {"MST",      -HM2SEC(700)},
    {""},
    {"PDT",      -HM2SEC(700)},
    {""}, {""},
    {"MDT",      -HM2SEC(600)},
    {""},
    {"AKST",     -HM2SEC(900)},
    {""}, {""},
    {"AEST",     HM2SEC(1000)},
    {""},
    {"AKDT",     -HM2SEC(800)},
    {""}, {""},
    {"AEDT",     HM2SEC(1100)},
    {"PHT",       HM2SEC(800)},
    {"NZT",      HM2SEC(1200)},
    {""}, {""},
    {"YST",      -HM2SEC(900)},
    {""},
    {"NZST",     HM2SEC(1200)},
    {""}, {""},
    {"YDT",      -HM2SEC(800)},
    {""},
    {"NZDT",     HM2SEC(1300)},
    {""}, {""},
    {"EET",       HM2SEC(200)},
    {""},
    {"WET",         HM2SEC(0)},
    {""}, {""},
    {"EEST",      HM2SEC(300)},
    {""},
    {"WEST",        HM2SEC(0)},
    {""}, {""},
    {"CAT",     -HM2SEC(1000)},
    {""},
    {"SAT",      -HM2SEC(400)},
    {""}, {""},
    {"CAST",      HM2SEC(930)},
    {""},
    {"SAST",      HM2SEC(200)},
    {""}, {""},
    {"CADT",     HM2SEC(1030)},
    {"GMT",         HM2SEC(0)},
    {"AMT",      -HM2SEC(400)},
    {""}, {""},
    {"MSK",       HM2SEC(300)},
    {""},
    {"AMST",     -HM2SEC(300)},
    {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {"MET",       HM2SEC(100)},
    {""},
    {"MEWT",      HM2SEC(100)},
    {""}, {""},
    {"MEST",      HM2SEC(200)},
    {""},
    {"FNT",      -HM2SEC(200)},
    {""}, {""},
    {"METDST",    HM2SEC(200)},
    {""},
    {"FNST",     -HM2SEC(100)},
    {""}, {""},
    {"EAT",       HM2SEC(300)},
    {""},
    {"WAT",      -HM2SEC(100)},
    {""}, {""},
    {"EAST",     HM2SEC(1000)},
    {""}, {""}, {""}, {""},
    {"EADT",     HM2SEC(1100)},
    {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {""}, {""}, {""}, {""}, {""}, {""},
    {"JAVT",      HM2SEC(700)},
    {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
    {""}, {""}, {""}, {""}, {""}, {""},
    {"MEZ",       HM2SEC(100)},
    {""}, {""}, {""}, {""},
    {"MESZ",      HM2SEC(200)}
  };

#ifdef __GNUC__
__inline
#endif
static unsigned int
tz_hash (register const char *str, register unsigned int len)
{
  static const unsigned short asso_values[] =
    {
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266,  10,   5,   0, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 120,  25,  40,  10,  70,
       50,  77,  24,  29,   0,  67,  10, 117,  85,   2,
        0,  82,  19,   5,  12, 266,  27,   2,  52, 266,
      105,   0, 266, 266, 266, 266, 266, 120,  25,  40,
       10,  70,  50,  77,  24,  29,   0,  67,  10, 117,
       85,   2,   0,  82,  19,   5,  12, 266,  27,   2,
       52, 266, 105,   0, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266, 266, 266, 266,
      266, 266, 266, 266, 266, 266, 266
    };
  register int hval = 0;

  switch (len)
    {
      default:
        hval += asso_values[(unsigned char)str[3]];
      /*FALLTHROUGH*/
      case 3:
        hval += asso_values[(unsigned char)str[2]];
      /*FALLTHROUGH*/
      case 2:
        hval += asso_values[(unsigned char)str[1]];
      /*FALLTHROUGH*/
      case 1:
        hval += asso_values[(unsigned char)str[0]+1];
        break;
    }
  return hval;
}

static const tz_map_t *
tz_lookup (register const char *str, register unsigned int len)
{
  if (len > 0)
    {
      register int key = tz_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = date_manip_tz_wordlist[key].name;

          if (strcasecmp(str, s) == 0)
            {
              return &date_manip_tz_wordlist[key];
            }
        }
    }
  return 0;
}
#endif /* USE_GPERF */

/**
 * Timezone parsing code based loosely on the algorithm in
 * filldata.cpp of MythTV.
 */
static time_t parse_xmltv_timezone( const char *tzstr, unsigned int len )
{
    time_t result = 0;

    if ((len == 5) && (tzstr[ 0 ] == '+')) {

        result = (3600 * ATOI_2(tzstr + 1)) + (60 * ATOI_2(tzstr + 3));

    } else if ((len == 5) && (tzstr[ 0 ] == '-')) {

        result = - (3600 * ATOI_2(tzstr + 1)) + (60 * ATOI_2(tzstr + 3));

    } else {
#ifndef USE_GPERF
        int i;

        for( i = 0; i < num_timezones; i++ ) {
            if( !strcasecmp( tzstr, date_manip_timezones[ i ].name ) ) {
                result = date_manip_timezones[ i ].offset;
                break;
            }
        }
#else /* USE_GPERF */
        const tz_map_t * tz = tz_lookup(tzstr, len);
        if (tz != 0)
        {
            result = tz->offset;
        }
#endif
    }

    return result;
}

time_t parse_xmltv_date_v5( const char *date, unsigned int full_len )
{
    const char * p;
    struct tm tm_obj;
    time_t tz;
    int len;
    int success = 1;
    time_t result = 0;

    /*
     * For some reason, mktime() accepts broken-time arguments as localtime,
     * and there is no corresponding UTC function. *Sigh*.
     * For this reason we have to calculate the offset from GMT to adjust the
     * argument given to mktime().
     */
    time_t now = time( 0 );
#ifndef WIN32
    long gmtoff = localtime( &now )->tm_gmtoff;
#else
    long gmtoff;
    struct tm * pTm = localtime( &now );
    gmtoff = 60*60 * pTm->tm_isdst - timezone;
#endif

    /*
     * according to the xmltv dtd:
     *
     * All dates and times in this [the xmltv] DTD follow the same format,
     * loosely based on ISO 8601.  They can be 'YYYYMMDDhhmmss' or some
     * initial substring, for example if you only know the year and month you
     * can have 'YYYYMM'.  You can also append a timezone to the end; if no
     * explicit timezone is given, UT is assumed.  Examples:
     * '200007281733 BST', '200209', '19880523083000 +0300'.  (BST == +0100.)
     *
     * thus:
     * example *date = "20031022220000 +0200"
     * type:            YYYYMMDDhhmmss ZZzzz"
     * position:        0         1         2          
     *                  012345678901234567890
     *
     * note: since part of the time specification can be omitted, we cannot
     *       hard-code the offset to the timezone!
     */

    /* Find where the timezone starts */
    p = date;
    while ((*p >= '0') && (*p <= '9'))
        p++;
    /* Calculate the length of the date */
    len = p - date;

    if (*p == ' ')
    {
        /* Parse the timezone, skipping the initial space */
        tz = parse_xmltv_timezone( p + 1, full_len - len - 1 );
    } else if (*p == 0) {
        /* Assume UT */
        tz = 0;
    } else {
        /* syntax error */
        tz = 0;
        success = 0;
    }

    if (success)
    {
        if (len >= DATE_OFF_SEC + 2)
        {
            tm_obj.tm_sec = ATOI_2(date + DATE_OFF_SEC);
        }
        else
            tm_obj.tm_sec = 0;

        if (len >= DATE_OFF_MIN + 2)
        {
            tm_obj.tm_min = ATOI_2(date + DATE_OFF_MIN);
        }
        else
            tm_obj.tm_min = 0;

        if (len >= DATE_OFF_HOUR + 2)
        {
            tm_obj.tm_hour = ATOI_2(date + DATE_OFF_HOUR);
        }
        else
            tm_obj.tm_hour = 0;

        if (len >= DATE_OFF_DAY + 2)
        {
            tm_obj.tm_mday = ATOI_2(date + DATE_OFF_DAY);
            tm_obj.tm_mon = ATOI_2(date + DATE_OFF_MON);
            tm_obj.tm_year = ATOI_4(date + DATE_OFF_YEAR);

            tm_obj.tm_sec = tm_obj.tm_sec - tz + gmtoff;
            tm_obj.tm_mon -= 1;
            tm_obj.tm_year -= 1900;
            tm_obj.tm_isdst = -1;

            result = mktime( &tm_obj );
        }
    }
    return result;
}

// ----------------------------------------------------------------------------

time_t parse_xmltv_date_v6( const char *date, unsigned int len )
{
   struct tm t;
   int nscan;
   int scan_pos;
   time_t tval;

   nscan = sscanf(date, "%4u-%2u-%2uT%2u:%2u:%2uZ%n",
                        &t.tm_year, &t.tm_mon, &t.tm_mday,
                        &t.tm_hour, &t.tm_min, &t.tm_sec, &scan_pos);
   if ((nscan >= 6) && (date[scan_pos] == 0))
   {
      t.tm_year -= 1900;
      t.tm_mon -= 1;
      t.tm_isdst = -1;
      t.tm_sec = 0;

      tval = mktime(&t);
      tval += 60*60 * t.tm_isdst - timezone;
   }
   else
      tval = 0;

   return tval;
}

