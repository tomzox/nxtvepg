/*
 *  Network codes and name tables
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
 *    The tables in this module have been takes from ETSI TR 101 231
 *    (the most up-to-date version is available at http://www.ebu.ch/)
 *    The module offers one service: search the table for a name for
 *    the given 16-bit NI or 12-bit VPS value.
 *
 *  Author: Tom Zoerner
 *
 *  $Id: cni_tables.c,v 1.35 2020/06/22 07:10:42 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/cni_tables.h"


typedef struct
{
   ushort       pdc;
   ushort       ni;
   const char * name;
} CNI_PDC_DESC;

typedef struct
{
   uchar        code;
   short        lto;
   const char * name;
} CNI_COUNTRY_DESC;


// ---------------------------------------------------------------------------
// NI and PDC table
//
static const CNI_PDC_DESC cni_pdc_desc_table[] =
{
   // Austria
   {0x1AC1, 0x4301, "ORF-1"},
   {0x1AC2, 0x4302, "ORF-2"},
   {0x1AC3, 0x0000, "ORF - FS 3"},
   {0x1AC3, 0x0000, "ORF-Sport +"},
   {0x1AC7, 0x0000, "ORF III"},
   {0x1AC8, 0x0000, "Nickelodeon"},
   {0x1AC9, 0x0000, "Comedy Central"},
   {0x1ACA, 0x0000, "ATV+"},
   {0x1ACB, 0x0000, "ORF- FS 2: Lokalprogramm Burgenland"},
   {0x1ACC, 0x0000, "ORF- FS 2: Lokalprogramm Kärnten"},
   {0x1ACD, 0x0000, "ORF- FS 2: Lokalprogramm Niederösterreich"},
   {0x1ACE, 0x0000, "ORF- FS 2: Lokalprogramm Oberösterreich"},
   {0x1ACF, 0x0000, "ORF- FS 2: Lokalprogramm Salzburg"},
   {0x1AD0, 0x0000, "ORF- FS 2: Lokalprogramm Steiermark"},
   {0x1AD1, 0x0000, "ORF- FS 2: Lokalprogramm Tirol"},
   {0x1AD2, 0x0000, "ORF- FS 2: Lokalprogramm Vorarlberg"},
   {0x1AD3, 0x0000, "ORF- FS 2: Lokalprogramm Wien"},
   {0x1A00, 0x8888, "OKTO"},
   {0x1ADE, 0x0000, "ATV Privat TV 2"},
   // Belgium
   {0x1604, 0x0404, "VT4"},
   {0x1601, 0x3201, "VRT TV1"},
   {0x1602, 0x3202, "CANVAS"},
   {0x1600, 0x3203, "RTBF 1"},
   {0x1600, 0x3204, "RTBF 2"},
   {0x1605, 0x3205, "VTM"},
   {0x1606, 0x3206, "Kanaal2"},
   {0x1600, 0x3207, "RTBF Sat"},
   {0x1600, 0x3208, "RTBF future use"},
   {0x1600, 0x320C, "AB3"},
   {0x1600, 0x320D, "AB4e"},
   {0x1600, 0x320E, "Ring TV"},
   {0x1600, 0x320F, "JIM.tv"},
   {0x1600, 0x3210, "RTV-Kempen"},
   {0x1600, 0x3211, "RTV-Mechelen"},
   {0x1600, 0x3212, "MCM Belgium"},
   {0x1600, 0x3213, "Vitaya"},
   {0x1600, 0x3214, "WTV"},
   {0x1600, 0x3215, "FocusTV"},
   {0x1600, 0x3216, "Be 1 ana"},
   {0x1600, 0x3217, "Be 1 num"},
   {0x1600, 0x3218, "Be Ciné 1"},
   {0x1600, 0x3219, "Be Sport 1"},
   {0x1600, 0x321A, "PRIME Sport 1"},
   {0x1600, 0x321B, "PRIME SPORT 2"},
   {0x1600, 0x321C, "PRIME Action"},
   {0x1600, 0x321D, "PRIME One"},
   {0x1600, 0x321E, "TV Brussel"},
   {0x1600, 0x321F, "AVSe"},
   {0x1600, 0x3220, "S televisie"},
   {0x1600, 0x3221, "TV Limburg"},
   {0x1600, 0x3222, "Kanaal 3"},
   {0x1600, 0x3223, "ATV"},
   {0x1600, 0x3224, "ROB TV"},
   {0x1600, 0x3226, "Sporza"},
   {0x1600, 0x3227, "VIJF tv"},
   {0x1600, 0x3228, "Life!tv"},
   {0x1600, 0x3229, "MTV Belgium (French)"},
   {0x1600, 0x322A, "EXQI Sport NL"},
   {0x1600, 0x322B, "EXQI Culture NL"},
   {0x1600, 0x322C, "Acht"},
   {0x1600, 0x322D, "EXQI Sport FR"},
   {0x1600, 0x322E, "EXQI Culture FR"},
   {0x1600, 0x322F, "Discovery Flanders"},
   {0x1600, 0x3230, "Télé Bruxelles"},
   {0x1600, 0x3231, "Télésambre"},
   {0x1600, 0x3232, "TV Com"},
   {0x1600, 0x3233, "Canal Zoom"},
   {0x1600, 0x3234, "Vidéoscope"},
   {0x1600, 0x3235, "Canal C"},
   {0x1600, 0x3236, "Télé MB"},
   {0x1600, 0x3237, "Antenne Centre"},
   {0x1600, 0x3238, "Télévesdre"},
   {0x1600, 0x3239, "RTC Télé Liège"},
   {0x1600, 0x323A, "EXQI Plus NL"},
   {0x1600, 0x323B, "EXQI Plus FR"},
   {0x1600, 0x323C, "EXQI Life NL"},
   {0x1600, 0x323D, "EXQI Life FR"},
   {0x1600, 0x323E, "EXQI News NL"},
   {0x1600, 0x323F, "EXQI News FR"},
   {0x1600, 0x3240, "No tele"},
   {0x1600, 0x3241, "TV Lux"},
   {0x1600, 0x3242, "Radio Contact Vision"},
   {0x1600, 0x3243, "vtmKzoom"},
   {0x1600, 0x3244, "NJAM"},
   {0x1600, 0x3245, "Ketnet op 12"},
   {0x1600, 0x3246, "MENT TV"},
   {0x1600, 0x3247, "National Geographic Channel Belgium"},
   {0x1600, 0x3248, "Libelle TV"},
   {0x1600, 0x3249, "Lacht"},
   {0x1600, 0x324A, "DOBBiT TV"},
   {0x1600, 0x324B, "Studio 100 TV"},
   {0x1600, 0x324C, "TLC Flanders"},
   {0x1600, 0x324D, "BEL RTL TELEVISION"},
   {0x1600, 0x325A, "Kanaal Z - NL"},
   {0x1600, 0x325B, "CANAL Z - FR"},
   {0x1600, 0x326A, "CARTOON Network - NL"},
   {0x1600, 0x326B, "CARTOON Network - FR"},
   {0x1600, 0x327A, "LIBERTY CHANNEL - NL"},
   {0x1600, 0x327B, "LIBERTY CHANNEL - FR"},
   {0x1600, 0x328A, "TCM - NL"},
   {0x1600, 0x328B, "TCM - FR"},
   {0x1600, 0x3298, "Mozaiek/Mosaique"},
   {0x1600, 0x3299, "Info Kanaal/Canal Info"},
   {0x1600, 0x32A7, "Be 1 + 1h"},
   {0x1600, 0x32A8, "Be Ciné 2"},
   {0x1600, 0x32A9, "Be Sport 2"},
   {0x1600, 0x3131, "TMF Belgium (Dutch)"},
   {0x1600, 0x32AA, "Nickelodeon/MTV Belgium (FR, shared)"},
   {0x1600, 0x32AB, "Nickelodeon Belgium (Dutch)"},
   {0x1600, 0x32AC, "Nickelodeon Belgium (French)"},
   {0x1600, 0x32AD, "Nick Jr Belgium (Dutch)"},
   {0x1600, 0x32AE, "Nick Jr (French)"},
   {0x1600, 0x32AF, "MTV Belgium (Dutch)"},
   {0x1600, 0x32B0, "anne"},
   {0x1600, 0x32B1, "vtmKzoom+"},
   // Croatia
   {0x4600, 0x0385, "HRT1"},
   {0x4600, 0x0386, "NovaTV"},
   {0x4600, 0x0387, "HRT2"},
   {0x4600, 0x0388, "HRT PLUS"},
   {0x4600, 0x0389, "RTL Kockica"},
   {0x4600, 0x0400, "RTL Televizija"},
   {0x4600, 0x0401, "RTL Living"},
   {0x4600, 0x0402, "TV Nova"},
   {0x4600, 0x0403, "RI-TV"},
   {0x4600, 0x0405, "Kanal RI"},
   {0x4600, 0x0406, "NIT"},
   {0x4600, 0x0407, "Kapital Network"},
   {0x4600, 0x0408, "Vox TV Zadar"},
   {0x4600, 0x0409, "GTV Zadar"},
   {0x4600, 0x040A, "TV Sibenik"},
   {0x4600, 0x040B, "VTV Varazdin"},
   {0x4600, 0x040C, "TV Cakovec"},
   {0x4600, 0x040D, "TV Jadran"},
   {0x4600, 0x040E, "Z1"},
   {0x4600, 0x040F, "OTV"},
   {0x4600, 0x0410, "24sata.tv"},
   {0x4600, 0x0411, "5 Kanal"},
   {0x4600, 0x0412, "B1 TV (Bjelovarska TV)"},
   {0x4600, 0x0413, "Croatian Music Channel"},
   {0x4600, 0x0414, "Dubrovacka TV"},
   {0x4600, 0x0415, "Infokanal Split"},
   {0x4600, 0x0416, "Kutina TV"},
   {0x4600, 0x0417, "Mini TV (NovaTV-djecji)"},
   {0x4600, 0x0418, "Narodna TV"},
   {0x4600, 0x0419, "Nezavisna televizija"},
   {0x4600, 0x041A, "Osjecka televizija"},
   {0x4600, 0x041B, "RK TV"},
   {0x4600, 0x041C, "Saborska TV"},
   {0x4600, 0x041D, "Samoborska TV"},
   {0x4600, 0x041E, "Slavonsko-brodska Televizija"},
   {0x4600, 0x041F, "Splitska TV"},
   {0x4600, 0x0420, "Televizija Slavonije i Baranje"},
   {0x4600, 0x0421, "Tportal.hr"},
   {0x4600, 0x0422, "TV 4R"},
   {0x4600, 0x0423, "TV Dalmacija"},
   {0x4600, 0x0424, "TV Dugi Rat"},
   {0x4600, 0x0425, "TV Plus"},
   {0x4600, 0x0426, "TV Turopolje"},
   {0x4600, 0x0427, "TV Velika Gorica"},
   {0x4600, 0x0428, "Vecernji HR"},
   {0x4600, 0x0429, "Vinkovacka televizija"},
   {0x4600, 0x042A, "Zagrebacka Kablovska Televizija"},
   {0x4600, 0x042B, "Doma TV"},
   {0x4600, 0x042C, "RTL 2"},
   {0x4600, 0x042D, "HOO - Sport"},
   {0x4600, 0x042E, "HRT3"},
   {0x4600, 0x042F, "HRT4"},
   {0x4600, 0x0430, "RTL Crime"},
   {0x4600, 0x0431, "RTL Passion"},
   // Czech Republic
   {0x32C1, 0x4201, "CT 1"},
   {0x32C2, 0x4202, "CT 2"},
   {0x32C3, 0x4203, "NOVA TV"},
   {0x32C4, 0x4204, "Prima TV"},
   {0x3200, 0x4205, "TV Praha"},
   {0x3200, 0x4206, "TV HK"},
   {0x3200, 0x4207, "TV Pardubice"},
   {0x3200, 0x4208, "TV Brno"},
   {0x32CA, 0x420A, "CT24"},
   {0x32CB, 0x420B, "CT4 SPORT"},
   {0x32CC, 0x420D, "CT5"},
   {0x3210, 0x4210, "Ocko TV"},
   {0x32D1, 0x4211, "CT1 Regional, Brno"},
   {0x32D2, 0x4212, "CT2 Regional, Brno"},
   {0x32D3, 0x4213, "NOVA CINEMA"},
   {0x32D4, 0x4214, "GALAXIE SPORT"},
   {0x32D5, 0x4215, "Prima Love"},
   {0x32D6, 0x4216, "Prima ZOOM"},
   {0x32D7, 0x4217, "Prima MAX"},
   {0x32DA, 0x421A, "CT24 Regional, Brno"},
   {0x32DC, 0x421C, "CT5 Regional, Brno"},
   {0x32E1, 0x4221, "CT1 Regional, Ostrava"},
   {0x32E2, 0x4222, "CT2 Regional, Ostrava"},
   {0x32EA, 0x422A, "CT24 Regional, Ostrava"},
   {0x32EB, 0x422B, "CT4 Regional, Ostrava"},
   {0x32EC, 0x422C, "CT5 Regional, Ostrava"},
   {0x32F1, 0x4231, "CT1 Regional"},
   {0x32F2, 0x4232, "CT2 Regional"},
   {0x32F3, 0x4233, "Z1 TV"},
   {0x32FA, 0x423A, "CT24 Regional"},
   {0x32FB, 0x423B, "CT4 Regional"},
   {0x32FC, 0x423C, "CT5 Regional"},
   // Denmark
   {0x2902, 0x4502, "TV 2"},
   {0x2904, 0x4503, "TV 2 Zulu"},
   {0x2900, 0x4504, "Discovery Denmark"},
   {0x2905, 0x4505, "TV 2 Charlie"},
   {0x2906, 0x4506, "TV Danmark"},
   {0x2907, 0x4507, "Kanal 5"},
   {0x2908, 0x4508, "TV 2 Film"},
   {0x2909, 0x4509, "TV 2 News"},
   {0x290A, 0x450A, "TV 2 FRI"},
   {0x2903, 0x49CF, "DR2"},
   {0x2901, 0x7392, "DR1"},
   // Finland
   {0x2601, 0x3581, "YLE1"},
   {0x2602, 0x3582, "YLE2"},
   {0x260F, 0x358F, "OWL3"},
   // France
   {0x2F0A, 0x330A, "Arte / La Cinquième"},
   {0x2F11, 0x3311, "RFO1"},
   {0x2F12, 0x3312, "RFO2"},
   {0x2F20, 0x3320, "Aqui TV"},
   {0x2F21, 0x3321, "TLM"},
   {0x2F22, 0x3322, "TLT"},
   {0x2F00, 0x33B2, "Sailing Channel"},
   {0x2FC1, 0x33C1, "AB1"},
   {0x2FC2, 0x33C2, "Canal J"},
   {0x2FC3, 0x33C3, "Canal Jimmy"},
   {0x2FC4, 0x33C4, "LCI"},
   {0x2FC5, 0x33C5, "La Chaîne Météo"},
   {0x2FC6, 0x33C6, "MCM"},
   {0x2FC7, 0x33C7, "TMC Monte-Carlo"},
   {0x2FC8, 0x33C8, "Paris Première"},
   {0x2FC9, 0x33C9, "Planète"},
   {0x2FCA, 0x33CA, "Série Club"},
   {0x2FCB, 0x33CB, "Télétoon"},
   {0x2FCC, 0x33CC, "Téva"},
   {0x2FD1, 0x33D1, "Disney Channel"},
   {0x2FD2, 0x33D2, "Disney Channel+1"},
   {0x2FD3, 0x33D3, "Disney XD"},
   {0x2FD4, 0x33D4, "Disney Junior"},
   {0x2F01, 0x33F1, "TF1"},
   {0x2F02, 0x33F2, "France 2"},
   {0x2F03, 0x33F3, "France 3"},
   {0x2F04, 0x33F4, "Canal+"},
   {0x2F06, 0x33F6, "M6"},
   {0x2FE2, 0xF101, "Eurosport"},
   {0x2FE3, 0xF102, "Eurosport2"},
   {0x2FE4, 0xF103, "Eurosportnews"},
   {0x2FE5, 0xF500, "TV5"},
   {0x2FE1, 0xFE01, "Euronews"},
   // Germany
   {0x1D00, 0x4904, "OK54 Bürgerrundfunk Trier"},  // VPS: 0xD44 used by ARD-alpha!
   {0x1D00, 0x4905, "Channel21"},
   {0x1D00, 0x490E, "RTL A"},
   {0x1D00, 0x490F, "RTL CH"},
   {0x1D00, 0x4910, "VOX A"},
   {0x1D00, 0x4911, "VOX CH"},
   {0x1D00, 0x4912, "RTL 2"},
   {0x1D00, 0x4913, "RTL 2 A"},
   {0x1D00, 0x4914, "RTL 2 CH"},
   {0x1D00, 0x4915, "RTL Nitro"},
   {0x1D00, 0x4916, "RTL Nitro A"},
   {0x1D00, 0x4917, "RTL Nitro CH"},
   {0x1D00, 0x4918, "GEO Television"},
   {0x1D41, 0x4941, "Festival"},
   {0x1D42, 0x4942, "MUXX"},
   {0x1D43, 0x4943, "EXTRA"},
   {0x1D44, 0x4944, "ARD-Alpha"},   // table states VPS: 0xDC6 now, but 0xD44 transmitted
   {0x1D72, 0x0000, "DMAX"},
   {0x1D73, 0x0000, "MTV"},
   {0x1D74, 0x0000, "Nick"},
   {0x1D75, 0x0000, "KDG Info"},
   {0x1D76, 0x0000, "Das Vierte"},
   {0x1D77, 0x49BD, "1-2-3.TV"},
   {0x1D78, 0x49BE, "Tele-5"},
   {0x1D79, 0x0000, "Channel 21"},
   {0x1D7A, 0x0000, "n24"},
   {0x1D7B, 0x0000, "TV.B"},
   {0x1D7C, 0x0000, "ONYX-TV"},
   {0x1D7D, 0x5C49, "QVC-Teleshopping"},
   {0x1D7F, 0x49BF, "Home Shopping Europe"},
   {0x1D81, 0x0000, "RBB-1: Regionalprogramm"},
   {0x1D82, 0x4982, "RBB-3: Ostdeutscher Rundfunk Brandenburg"},
   {0x1D83, 0x0000, "Parlamentsfernsehen"},
   {0x1D85, 0x490A, "Arte"},
   {0x1D87, 0x0000, "1A-Fernsehen"},
   {0x1D88, 0x0000, "VIVA"},
   {0x1D89, 0x0000, "Comedy Central"},
   {0x1D8A, 0x0000, "Super RTL"},
   {0x1D8B, 0x0000, "RTL Club"},
   {0x1D8C, 0x0000, "n-tv"},
   {0x1D8D, 0x0000, "Deutsches Sportfernsehen"},
   {0x1D8E, 0x490C, "VOX Fernsehen"},
   {0x1D8F, 0x491d, "RTL 2"},
   {0x1D90, 0x0000, "RTL 2: regional"},
   {0x1D91, 0x0000, "Eurosport"},
   {0x1D92, 0x0000, "Kabel 1"},
   {0x1D93, 0x0000, "Sixx"},
   {0x1D94, 0x0000, "Pro Sieben"},
   {0x1D95, 0x0000, "SAT.1: Brandenburg"},
   {0x1D96, 0x0000, "SAT.1: Thüringen"},
   {0x1D97, 0x0000, "SAT.1: Sachsen"},
   {0x1D98, 0x0000, "SAT.1: Mecklenburg-Vorpommern"},
   {0x1D99, 0x0000, "SAT.1: Sachsen-Anhalt"},
   {0x1D9A, 0x0000, "RTL: Regional"},
   {0x1D9B, 0x0000, "RTL: Schleswig-Holstein"},
   {0x1D9C, 0x0000, "RTL: Hamburg"},
   {0x1D9D, 0x0000, "RTL: Berlin"},
   {0x1D9E, 0x0000, "RTL: Niedersachsen"},
   {0x1D9F, 0x0000, "RTL: Bremen"},
   {0x1DA0, 0x0000, "RTL: Nordrhein-Westfalen"},
   {0x1DA1, 0x0000, "RTL: Hessen"},
   {0x1DA2, 0x0000, "RTL: Rheinland-Pfalz"},
   {0x1DA3, 0x0000, "RTL: Baden-Württemberg"},
   {0x1DA4, 0x0000, "RTL: Bayern"},
   {0x1DA5, 0x0000, "RTL: Saarland"},
   {0x1DA6, 0x0000, "RTL: Sachsen-Anhalt"},
   {0x1DA7, 0x0000, "RTL: Mecklenburg-Vorpommern"},
   {0x1DA8, 0x0000, "RTL: Sachsen"},
   {0x1DA9, 0x0000, "RTL: Thüringen"},
   {0x1DAA, 0x0000, "RTL: Brandenburg"},
   {0x1DAB, 0x490D, "RTL Plus"},
   {0x1DAC, 0x0000, "Premiere (Pay TV)"},
   {0x1DAD, 0x0000, "SAT.1: Regional"},
   {0x1DAE, 0x0000, "SAT.1: Schleswig-Holstein"},
   {0x1DAF, 0x0000, "SAT.1: Hamburg"},
   {0x1DB0, 0x0000, "SAT.1: Berlin"},
   {0x1DB1, 0x0000, "SAT.1: Niedersachsen"},
   {0x1DB2, 0x0000, "SAT.1: Bremen"},
   {0x1DB3, 0x0000, "SAT.1: Nordrhein-Westfalen"},
   {0x1DB4, 0x0000, "SAT.1: Hessen"},
   {0x1DB5, 0x0000, "SAT.1: Rheinland-Pfalz"},
   {0x1DB6, 0x0000, "SAT.1: Baden-Württemberg"},
   {0x1DB7, 0x0000, "SAT.1: Bayern"},
   {0x1DB8, 0x0000, "SAT.1: Saarland"},
   {0x1DB9, 0x0000, "SAT.1"},
   {0x1DBA, 0x0000, "9live"},
   {0x1DBB, 0x0000, "Deutsche Welle Fernsehen Berlin"},
   {0x1DBD, 0x0000, "Berlin-Offener Kanal"},
   {0x1DBE, 0x0000, "Berlin-Mix-Channel II"},
   {0x1DBF, 0x0000, "Berlin-Mix-Channel I"},
   {0x1DC1, 0x4901, "ARD: Erstes Deutsches Fernsehen"},
   {0x1DC2, 0x4902, "ZDF: Zweites Deutsches Fernsehen"},
   {0x1DC7, 0x49C7, "3sat (ARD/ZDF/ORF/SRG common programme)"},
   {0x1DC8, 0x4908, "Phoenix: Ereignis und Dokumentationskanal (ARD/ZDF)"},
   {0x1DC9, 0x49C9, "Kinderkanal (ARD/ZDF)"},
   {0x1DCA, 0x0000, "BR-1: Regionalprogramm"},
   {0x1DCB, 0x49CB, "BR-3: Bayerischer Rundfunk"},
   {0x1DCC, 0x0000, "BR-3: Süd"},
   {0x1DCD, 0x0000, "BR-3: Nord"},
   {0x1DCE, 0x49FF, "HR-1: Regionalprogramm"},
   {0x1DCF, 0x0000, "HR-3: Hessischer Rundfunk"},
   {0x1DD0, 0x0000, "NDR-1: Landesprogramm dreiländerweit"},
   {0x1DD1, 0x0000, "NDR-1: Landesprogramm Hamburg"},
   {0x1DD2, 0x0000, "NDR-1: Landesprogramm Niedersachsen"},
   {0x1DD3, 0x0000, "NDR-1: Landesprogramm Schleswig-Holstein"},
   {0x1DD4, 0x49D4, "NDR-3: Norddeutscher Rundfunk (NDR, SFB, RB)"},
   {0x1DD5, 0x0000, "NDR-3: dreiländerweit"},
   {0x1DD6, 0x0000, "NDR-3: Hamburg"},
   {0x1DD7, 0x0000, "NDR-3: Niedersachsen"},
   {0x1DD8, 0x0000, "NDR-3: Schleswig-Holstein"},
   {0x1DD9, 0x49D9, "RB-1: Regionalprogramm"},
   {0x1DDA, 0x0000, "RB-3 (separation from Nord 3)"},
   {0x1DDB, 0x0000, "SFB-1: Regionalprogramm"},
   {0x1DDC, 0x49DC, "SFB-3: Sender Freies Berlin"},
   {0x1DDD, 0x0000, "SDR-1 + SWF-1: Regionalprogramm Baden-Württemberg"},
   {0x1DDE, 0x0000, "SWF-1: Regionalprogramm Rheinland-Pfalz"},
   {0x1DDF, 0x49DF, "SR-1: Regionalprogramm"},
   {0x1DE0, 0x0000, "SWR-3 (Südwest 3), Verbund 3 Programme SDR, SR, SWF"},
   {0x1DE1, 0x49E1, "SWR-3: Regionalprogramm Baden-Württemberg"},
   {0x1DE2, 0x0000, "SWR-3: Regionalprogramm Saarland"},
   {0x1DE3, 0x0000, "SWR-3: Regionalprogramm Baden-Württemberg Süd"},
   {0x1DE4, 0x49E4, "SWR-3: Regionalprogramm Rheinland-Pfalz"},
   {0x1DE5, 0x0000, "WDR-1: Regionalprogramm"},
   {0x1DE6, 0x49E6, "WDR-3: Westdeutscher Rundfunk"},
   {0x1DE7, 0x0000, "WDR-3: Bielefeld"},
   {0x1DE8, 0x0000, "WDR-3: Dortmund"},
   {0x1DE9, 0x0000, "WDR-3: Düsseldorf"},
   {0x1DEA, 0x0000, "WDR-3: Köln"},
   {0x1DEB, 0x0000, "WDR-3: Münster"},
   {0x1DEC, 0x0000, "SDR-1: Regionalprogramm"},
   {0x1DED, 0x0000, "SW 3: Regionalprogramm Baden-Württemberg Nord"},
   {0x1DEE, 0x0000, "SW 3: Regionalprogramm Mannheim"},
   {0x1DEF, 0x0000, "SDR-1 + SWF-1: Regionalprogramm BW+RP"},
   {0x1DF0, 0x0000, "SWF-1: Regionalprogramm"},
   {0x1DF1, 0x0000, "NDR-1: Landesprogramm Mecklenburg-Vorpommern"},
   {0x1DF2, 0x0000, "NDR-3: Mecklenburg-Vorpommern"},
   {0x1DF3, 0x0000, "MDR-1: Landesprogramm Sachsen"},
   {0x1DF4, 0x0000, "MDR-3: Sachsen"},
   {0x1DF5, 0x0000, "MDR: Dresden"},
   {0x1DF6, 0x0000, "MDR-1: Landesprogramm Sachsen-Anhalt"},
   {0x1DF7, 0x0000, "Lokal-Programm WDR-Dortmund"},
   {0x1DF8, 0x0000, "MDR-3: Sachsen-Anhalt"},
   {0x1DF9, 0x0000, "MDR: Magdeburg"},
   {0x1DFA, 0x0000, "MDR-1: Landesprogramm Thüringen"},
   {0x1DFB, 0x0000, "MDR-3: Thüringen"},
   {0x1DFC, 0x0000, "MDR: Erfurt"},
   {0x1DFD, 0x0000, "MDR-1: Regionalprogramm"},
   {0x1DFE, 0x49FE, "MDR-3: Mitteldeutscher Rundfunk"},
   // Greece
   {0x2101, 0x3001, "ET-1"},
   {0x2102, 0x3002, "NET"},
   {0x2103, 0x3003, "ET-3"},
   // Hungary
   {0x1B00, 0x3601, "MTV1"},
   {0x1B00, 0x3602, "MTV2"},
   {0x1B00, 0x3611, "MTV1 regional, Budapest"},
   {0x1B00, 0x3621, "MTV1 regional, Pécs"},
   {0x1B00, 0x3622, "tv2"},
   {0x1B00, 0x3631, "MTV1 regional, Szeged"},
   {0x1B00, 0x3636, "Duna Televizio"},
   {0x1B00, 0x3641, "MTV1 regional, Szombathely"},
   {0x1B00, 0x3651, "MTV1 regional, Debrecen"},
   {0x1B00, 0x3661, "MTV1 regional, Miskolc"},
   {0x1B00, 0x3681, "MTV1 future use"},
   {0x1B00, 0x3682, "MTV2 future use"},
   // Iceland
   {0x4200, 0x3541, "Rikisutvarpid-Sjonvarp"},
   // Ireland
   {0x4201, 0x3531, "RTE1"},
   {0x4202, 0x3532, "Network 2"},
   {0x4203, 0x3533, "Teilifis na Gaeilge"},
   {0x4200, 0x3333, "TV3"},
   // Italy
   {0x1500, 0x3901, "RAI 1"},
   {0x1500, 0x3902, "RAI 2"},
   {0x1500, 0x3903, "RAI 3"},
   {0x1500, 0x3904, "Rete A"},
   {0x1505, 0x3905, "Canale Italia"},
   {0x1500, 0x3906, "7 Gold - Telepadova"},
   {0x1500, 0x3907, "Teleregione"},
   {0x1500, 0x3908, "Telegenova"},
   {0x1500, 0x3909, "Telenova"},
   {0x1500, 0x390A, "Arte"},
   {0x1500, 0x390B, "Canale Dieci"},
   {0x1500, 0x3910, "TRS TV"},
   {0x1511, 0x3911, "Sky Cinema Classic"},
   {0x1512, 0x3912, "Sky Future use (canale 109)"},
   {0x1513, 0x3913, "Sky Calcio 1"},
   {0x1514, 0x3914, "Sky Calcio 2"},
   {0x1515, 0x3915, "Sky Calcio 3"},
   {0x1516, 0x3916, "Sky Calcio 4"},
   {0x1517, 0x3917, "Sky Calcio 5"},
   {0x1518, 0x3918, "Sky Calcio 6"},
   {0x1519, 0x3919, "Sky Calcio 7"},
   {0x1500, 0x391A, "TN8 Telenorba"},
   {0x1500, 0x3920, "RaiNotizie24"},
   {0x1500, 0x3921, "RAI Med"},
   {0x1500, 0x3922, "RAI Sport Più"},
   {0x1500, 0x3923, "RAI Edu1"},
   {0x1500, 0x3924, "RAI Edu2"},
   {0x1500, 0x3925, "RAI NettunoSat1"},
   {0x1500, 0x3926, "RAI NettunoSat2"},
   {0x1500, 0x3927, "CameraDeputati"},
   {0x1500, 0x3928, "Senato"},
   {0x1500, 0x3929, "RAI 4"},
   {0x1500, 0x392A, "RAI Gulp"},
   {0x1500, 0x3930, "Discovery Italy"},
   {0x1500, 0x3931, "MTV VH1"},
   {0x1500, 0x3933, "MTV Italia"},
   {0x1500, 0x3934, "MTV Brand New"},
   {0x1500, 0x3935, "MTV Hits"},
   {0x1500, 0x3936, "MTV GOLD"},
   {0x1500, 0x3937, "MTV PULSE"},
   {0x1500, 0x3938, "RTV38"},
   {0x1500, 0x3939, "GAY TV"},
   {0x1500, 0x393A, "TP9 Telepuglia"},
   {0x1500, 0x3940, "Video Italia"},
   {0x1500, 0x3941, "SAT 2000"},
   {0x1542, 0x3942, "Jimmy"},
   {0x1543, 0x3943, "Planet"},
   {0x1544, 0x3944, "Cartoon Network"},
   {0x1545, 0x3945, "Boomerang"},
   {0x1546, 0x3946, "CNN International"},
   {0x1547, 0x3947, "Cartoon Network +1"},
   {0x1548, 0x3948, "Sky Sports 3"},
   {0x1549, 0x3949, "Sky Diretta Gol"},
   {0x1500, 0x394A, "TG NORBA"},
   {0x1500, 0x3952, "RAISat Cinema"},
   {0x1500, 0x3954, "RAISat Gambero Rosso"},
   {0x1500, 0x3955, "RAISat YoYo"},
   {0x1500, 0x3956, "RAISat Smash"},
   {0x1500, 0x3959, "RAISat Extra"},
   {0x1500, 0x395A, "RAISat Premium"},
   {0x1560, 0x3960, "SCI FI CHANNEL"},
   {0x1500, 0x3961, "Discovery Civilisations"},
   {0x1500, 0x3962, "Discovery Travel and Adventure"},
   {0x1500, 0x3963, "Discovery Science"},
   {0x1568, 0x3968, "Sky Meteo24"},
   {0x1500, 0x3970, "Sky Cinema 2"},
   {0x1500, 0x3971, "Sky Cinema 3"},
   {0x1500, 0x3972, "Sky Cinema Autore"},
   {0x1500, 0x3973, "Sky Cinema Max"},
   {0x1500, 0x3974, "Sky Cinema 16:9"},
   {0x1500, 0x3975, "Sky Sports 2"},
   {0x1500, 0x3976, "Sky TG24"},
   {0x1577, 0x3977, "Fox"},
   {0x1578, 0x3978, "Foxlife"},
   {0x1579, 0x3979, "National Geographic Channel"},
   {0x1580, 0x3980, "A1"},
   {0x1581, 0x3981, "History Channel"},
   {0x1500, 0x3985, "FOX KIDS"},
   {0x1500, 0x3986, "PEOPLE TV - RETE 7"},
   {0x1500, 0x3987, "FOX KIDS +1"},
   {0x1500, 0x3988, "LA7"},
   {0x1500, 0x3989, "PrimaTV"},
   {0x1500, 0x398A, "SportItalia"},
   {0x1500, 0x398B, "Mediaset Future Use"},
   {0x1500, 0x398C, "Mediaset Future Use"},
   {0x1500, 0x398D, "Mediaset Future Use"},
   {0x1500, 0x398E, "Mediaset Future Use"},
   {0x1500, 0x398F, "Espansione TV"},
   {0x1590, 0x3990, "STUDIO UNIVERSAL"},
   {0x1591, 0x3991, "Marcopolo"},
   {0x1592, 0x3992, "Alice"},
   {0x1593, 0x3993, "Nuvolari"},
   {0x1594, 0x3994, "Leonardo"},
   {0x1596, 0x3996, "SUPERPIPPA CHANNEL"},
   {0x1500, 0x3997, "Sky Sports 1"},
   {0x1500, 0x3998, "Sky Cinema 1"},
   {0x1500, 0x3999, "Tele+3"},
   {0x150A, 0x399A, "FacileTV"},
   {0x150B, 0x399B, "Sitcom 2"},
   {0x150C, 0x399C, "Sitcom 3"},
   {0x150D, 0x399D, "Sitcom 4"},
   {0x150E, 0x399E, "Sitcom 5"},
   {0x150F, 0x399F, "Italiani nel Mondo"},
   {0x15A0, 0x39A0, "Sky Calcio 8"},
   {0x15A1, 0x39A1, "Sky Calcio 9"},
   {0x15A2, 0x39A2, "Sky Calcio 10"},
   {0x15A3, 0x39A3, "Sky Calcio 11"},
   {0x15A4, 0x39A4, "Sky Calcio 12"},
   {0x15A5, 0x39A5, "Sky Calcio 13"},
   {0x15A6, 0x39A6, "Sky Calcio 14"},
   {0x15A7, 0x39A7, "Telesanterno"},
   {0x15A8, 0x39A8, "Telecentro"},
   {0x15A9, 0x39A9, "Telestense"},
   {0x1500, 0x39AB, "TCS - Telecostasmeralda"},
   {0x15B0, 0x39B0, "Disney Channel +1"},
   {0x1500, 0x39B1, "Sailing Channel"},
   {0x15B2, 0x39B2, "Disney Channel"},
   {0x15B3, 0x39B3, "7 Gold-Sestra Rete"},
   {0x15B4, 0x39B4, "Rete 8-VGA"},
   {0x15B5, 0x39B5, "Nuovarete"},
   {0x15B6, 0x39B6, "Radio Italia TV"},
   {0x15B7, 0x39B7, "Rete 7"},
   {0x15B8, 0x39B8, "E! Entertainment Television"},
   {0x15B9, 0x39B9, "Toon Disney"},
   {0x1500, 0x39BA, "Play TV Italia"},
   {0x1500, 0x39C1, "La7 Cartapiù A"},
   {0x1500, 0x39C2, "La7 Cartapiù B"},
   {0x1500, 0x39C3, "La7 Cartapiù C"},
   {0x1500, 0x39C4, "La7 Cartapiù D"},
   {0x1500, 0x39C5, "La7 Cartapiù E"},
   {0x1500, 0x39C6, "La7 Cartapiù X"},
   {0x15C7, 0x39C7, "Bassano TV"},
   {0x15C8, 0x39C8, "ESPN Classic Sport"},
   {0x1500, 0x39CA, "VIDEOLINA"},
   {0x15D1, 0x39D1, "Mediaset Premium 5"},
   {0x15D2, 0x39D2, "Mediaset Premium 1"},
   {0x15D3, 0x39D3, "Mediaset Premium 2"},
   {0x15D4, 0x39D4, "Mediaset Premium 3"},
   {0x15D5, 0x39D5, "Mediaset Premium 4"},
   {0x15D6, 0x39D6, "BOING"},
   {0x15D7, 0x39D7, "Playlist Italia"},
   {0x15D8, 0x39D8, "MATCH MUSIC"},
   {0x1500, 0x39D9, "Televisiva SUPER3"},
   {0x1500, 0x39DA, "Mediashopping"},
   {0x1500, 0x39DB, "Mediaset Premium 6"},
   {0x1500, 0x39DC, "Mediaset Premium 7"},
   {0x1500, 0x39DD, "Mediaset Future Use"},
   {0x1500, 0x39DE, "Mediaset Future Use"},
   {0x1500, 0x39DF, "Iris"},
   {0x15E1, 0x39E1, "National Geographic +1"},
   {0x15E2, 0x39E2, "History Channel +1"},
   {0x15E3, 0x39E3, "Sky TV"},
   {0x15E4, 0x39E4, "GXT"},
   {0x15E5, 0x39E5, "Playhouse Disney"},
   {0x15E6, 0x39E6, "Sky Canale 224"},
   {0x1500, 0x39E7, "Music Box"},
   {0x1500, 0x39E8, "Tele Liguria Sud"},
   {0x1500, 0x39E9, "TN7 Telenorba"},
   {0x1500, 0x39EA, "Brescia Punto TV"},
   {0x1500, 0x39EB, "QOOB"},
   {0x1500, 0x39EE, "AB Channel"},
   {0x1500, 0x39F1, "Teleradiocity"},
   {0x1500, 0x39F2, "Teleradiocity Genova"},
   {0x1500, 0x39F3, "Teleradiocity Lombardia"},
   {0x1500, 0x39F4, "Telestar Piemonte"},
   {0x1500, 0x39F5, "Telestar Liguria"},
   {0x1500, 0x39F6, "Telestar Lombardia"},
   {0x1500, 0x39F7, "Italia 8 Piemonte"},
   {0x1500, 0x39F8, "Italia 8 Lombardia"},
   {0x1500, 0x39F9, "Radio Tele Europa"},
   {0x1500, 0x39FA, "Mediaset Future Use"},
   {0x1500, 0x39FB, "Mediaset Future Use"},
   {0x1500, 0x39FC, "Mediaset Future Use"},
   {0x1500, 0x39FD, "Mediaset Future Use"},
   {0x1500, 0x39FE, "Mediaset Future Use"},
   {0x1500, 0x39FF, "Mediaset Future Use"},
   {0x1500, 0xFA04, "Rete 4"},
   {0x1500, 0xFA05, "Canale 5"},
   {0x1500, 0xFA06, "Italia 1"},
   // Luxembourg
   {0x0000, 0x3209, "RTL-TVI"},
   {0x0000, 0x320A, "CLUB-RTL"},
   {0x0000, 0x3225, "PLUG TV"},
   {0x0000, 0x4000, "RTL Télé Lëtzebuerg"},  // VPS 0x791
   {0x0000, 0x4020, "RTL TVI 20 ANS"},
   // Netherlands
   {0x4801, 0x3101, "Nederland 1"},
   {0x4802, 0x3102, "Nederland 2"},
   {0x4803, 0x3103, "Nederland 3"},
   {0x4804, 0x3104, "RTL 4"},
   {0x4805, 0x3105, "RTL 5"},
   {0x4800, 0x3110, "RTV Noord"},
   {0x4800, 0x3111, "Omrop Fryslan"},
   {0x4800, 0x3112, "RTV Drenthe"},
   {0x4800, 0x3113, "RTV Oost"},
   {0x4800, 0x3114, "Omroep Gelderland"},
   {0x4800, 0x3116, "RTV Noord-Holland"},
   {0x4800, 0x3117, "RTV Utrecht"},
   {0x4800, 0x3118, "RTV West"},
   {0x4800, 0x3119, "RTV Rijnmond"},
   {0x4800, 0x311A, "Omroep Brabant"},
   {0x4800, 0x311B, "L1 RTV Limburg"},
   {0x4800, 0x311C, "Omroep Zeeland"},
   {0x4800, 0x311D, "Omroep Flevoland"},
   {0x4800, 0x311F, "BVN"},
   {0x4820, 0x3120, "The BOX"},
   {0x4800, 0x3121, "Discovery Netherlands"},
   {0x4822, 0x3122, "Nickelodeon"},
   {0x4800, 0x3123, "Animal Planet Benelux"},
   {0x4800, 0x3124, "TIEN"},
   {0x4800, 0x3125, "NET5"},
   {0x4800, 0x3126, "SBS6"},
   {0x4800, 0x3127, "SBS9"},
   {0x4800, 0x3128, "V8"},
   {0x4800, 0x3130, "TMF (Netherlands service)"},
   {0x4800, 0x3132, "MTV NL"},
   {0x4800, 0x3133, "Nickelodeon"},
   {0x4800, 0x3134, "The Box"},
   {0x4800, 0x3135, "TMF Pure"},
   {0x4800, 0x3136, "TMF Party"},
   {0x4800, 0x3137, "RNN7"},
   {0x4800, 0x3138, "TMF NL"},
   {0x4800, 0x3139, "Nick Toons"},
   {0x4800, 0x313A, "Nick Jr."},
   {0x4800, 0x313B, "Nick Hits"},
   {0x4800, 0x313C, "MTV Brand New"},
   {0x4800, 0x313D, "Disney Channel"},
   {0x4800, 0x313E, "Disney XD"},
   {0x4800, 0x313F, "Playhouse Disney"},
   {0x4800, 0x3146, "RTL Z"},
   {0x4847, 0x3147, "RTL 7"},
   {0x4848, 0x3148, "RTL 8"},
   {0x4800, 0x3150, "HET GESPREK"},
   {0x4800, 0x3151, "InfoThuis TV"},
   {0x4800, 0x3152, "Graafschap TV (Oost-Gelderland)"},
   {0x4800, 0x3153, "Gelre TV (Veluwe)"},
   {0x4800, 0x3154, "Gelre TV (Groot-Arnhem)"},
   {0x4800, 0x3155, "Gelre TV (Groot-Nijmegen)"},
   {0x4800, 0x3156, "Gelre TV (Betuwe)"},
   {0x4800, 0x3157, "Brabant 10 (West Brabant)"},
   {0x4800, 0x3158, "Brabant 10 (Midden Brabant)"},
   {0x4800, 0x3159, "Brabant 10 (Noord Oost Brabant)"},
   {0x4800, 0x315A, "Brabant 10 (Zuid Oost Brabant)"},
   {0x4800, 0x315B, "Regio22"},
   {0x4800, 0x315C, "Maximaal TV"},
   {0x4800, 0x315D, "GPTV"},
   {0x4800, 0x315E, "1TV (Groningen)"},
   {0x4800, 0x315F, "1TV (Drenthe)"},
   {0x4800, 0x3160, "Cultura 24"},
   {0x4800, 0x3161, "101 TV"},
   {0x4800, 0x3162, "Best 24"},
   {0x4800, 0x3163, "Holland Doc 24"},
   {0x4800, 0x3164, "Geschiedenis 24"},
   {0x4800, 0x3165, "Consumenten 24"},
   {0x4800, 0x3166, "Humor 24"},
   {0x4800, 0x3167, "Sterren 24"},
   {0x4800, 0x3168, "Spirit 24"},
   {0x4800, 0x3169, "Familie 24"},
   {0x4800, 0x316A, "Journaal 24"},
   {0x4800, 0x316B, "Politiek 24"},
   {0x4800, 0x316C, "NPO Future Use"},
   {0x4800, 0x316D, "NPO Future Use"},
   {0x4800, 0x316E, "NPO Future Use"},
   {0x4800, 0x316F, "NPO Future Use"},
   {0x4800, 0x3171, "RTV Rotterdam"},
   {0x4800, 0x3172, "Brug TV"},
   {0x4800, 0x3173, "TV Limburg"},
   {0x4800, 0x3174, "Kindernet / Comedy Central"},
   {0x4800, 0x3175, "Comedy Central Extra"},
   // Norway
   {0x3F00, 0x4701, "NRK1"},
   {0x3F00, 0x4702, "TV 2"},
   {0x3F00, 0x4703, "NRK2"},
   {0x3F00, 0x4704, "TV Norge"},
   {0x3F00, 0x4720, "Discovery Nordic"},
   // Poland
   {0x3300, 0x4801, "TVP1"},
   {0x3300, 0x4802, "TVP2"},
   {0x3300, 0x4810, "TV Polonia"},
   {0x3300, 0x4820, "TVN"},
   {0x3300, 0x4821, "TVN Siedem"},
   {0x3300, 0x4822, "TVN24"},
   {0x3300, 0x4830, "Discovery Poland"},
   {0x3300, 0x4831, "Animal Planet"},
   {0x3300, 0x4880, "TVP Warszawa"},
   {0x3300, 0x4881, "TVP Bialystok"},
   {0x3300, 0x4882, "TVP Bydgoszcz"},
   {0x3300, 0x4883, "TVP Gdansk"},
   {0x3300, 0x4884, "TVP Katowice"},
   {0x3300, 0x4886, "TVP Krakow"},
   {0x3300, 0x4887, "TVP Lublin"},
   {0x3300, 0x4888, "TVP Lodz"},
   {0x3300, 0x4890, "TVP Rzeszow"},
   {0x3300, 0x4891, "TVP Poznan"},
   {0x3300, 0x4892, "TVP Szczecin"},
   {0x3300, 0x4893, "TVP Wroclaw"},
   // Portugal
   {0x5800, 0x3510, "RTP1"},
   {0x5800, 0x3511, "RTP2"},
   {0x5800, 0x3512, "RTPAF"},
   {0x5800, 0x3513, "RTPI"},
   {0x5800, 0x3514, "RTPAZ"},
   {0x5800, 0x3515, "RTPM"},
   // San Marino
   {0x2200, 0x3781, "RTV"},
   // Slovakia
   {0x35A1, 0x42A1, "STV1"},
   {0x35A2, 0x42A2, "STV2"},
   {0x35A3, 0x42A3, "STV3"},
   {0x35A4, 0x42A4, "STV4"},
   {0x35B1, 0x42B1, "TV JOJ"},
   // Slovenia
   {0x0000, 0xAAE1, "SLO1"},
   {0x0000, 0xAAE2, "SLO2"},
   {0x0000, 0xAAE3, "KC"},
   {0x0000, 0xAAE4, "TLM"},
   {0x0000, 0xAAE5, "POP TV"},
   {0x0000, 0xAAE6, "KANAL A"},
   {0x0000, 0xAAF1, "SLO3"},
   // Spain
   {0x3E00, 0x3402, "ETB 2"},
   {0x3E00, 0x3403, "CANAL 9"},
   {0x3E00, 0x3404, "PUNT 2"},
   {0x3E00, 0x3405, "CCV"},
   {0x3E00, 0x3406, "CANAL 9 NEWS 24H Future use"},
   {0x3E00, 0x3407, "CANAL 9 Future Use"},
   {0x3E00, 0x3408, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3409, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x340A, "Arte"},
   {0x3E00, 0x340B, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x340C, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x340D, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x340E, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x340F, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3410, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3411, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3412, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3413, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3414, "CANAL 9 DVB Future Use"},
   {0x3E00, 0x3415, "Canal Extremadura TV"},
   {0x3E00, 0x3416, "Extremadura TV"},
   {0x3E20, 0x3420, "Telemadrid"},
   {0x3E21, 0x3421, "La Otra"},
   {0x3E22, 0x3422, "TM SAT"},
   {0x3E23, 0x3423, "La sexta"},
   {0x3E24, 0x3424, "Antena 3"},
   {0x3E25, 0x3425, "Neox"},
   {0x3E26, 0x3426, "Nova"},
   {0x3E27, 0x3427, "Cuatro"},
   {0x3E28, 0x3428, "CNN+"},
   {0x3E29, 0x3429, "40 Latino"},
   {0x3E2A, 0x342A, "24 Horas"},
   {0x3E2B, 0x342B, "Clan TVE"},
   {0x3E2C, 0x342C, "Teledeporte"},
   {0x3E00, 0x342D, "CyL7"},
   {0x3E00, 0x342E, "8TV"},
   {0x3E00, 0x342F, "EDC2"},
   {0x3E00, 0x3430, "EDC3"},
   {0x3E00, 0x3431, "105tv"},
   {0x3E00, 0x3432, "CyL8"},
   {0x3E00, 0x3E00, "TVE1"},
   {0x3E00, 0xBA01, "ETB 1"},
   {0x3E00, 0xCA03, "TV3"},
   {0x3E00, 0xCA33, "C33"},
   {0x3E00, 0xE100, "TVE2"},
   {0x3E00, 0xE200, "TVE Internacional Europa"},
   {0x1FE5, 0xE500, "Tele5"},
   {0x1FE6, 0xE501, "Tele5 Estrellas"},
   {0x1FE7, 0xE502, "Tele5 Sport"},
   // Sweden
   {0x4E00, 0x4600, "SVT Test Txmns"},
   {0x4E01, 0x4601, "SVT 1"},
   {0x4E02, 0x4602, "SVT 2"},
   {0x4E40, 0x4640, "TV 4"},
   // Switzerland
   {0x2481, 0x0000, "TeleZüri"},
   {0x2482, 0x0000, "Teleclub Abonnements-Fernsehen"},
   {0x2483, 0x0000, "TV Züri"},
   {0x2484, 0x0000, "TeleBern"},
   {0x2485, 0x0000, "Tele M1"},
   {0x2486, 0x0000, "Star TV"},
   {0x2487, 0x0000, "Pro Sieben"},
   {0x2488, 0x0000, "TopTV"},
   {0x2489, 0x0000, "Tele 24"},
   {0x248A, 0x0000, "Kabel 1"},
   {0x248B, 0x0000, "TV3"},
   {0x248C, 0x0000, "TeleZüri 2"},
   {0x248D, 0x0000, "Swizz Music Television"},
   {0x248E, 0x0000, "Intro TV"},
   {0x248F, 0x0000, "Tele Tell"},
   {0x2490, 0x0000, "Tele Top"},
   {0x2491, 0x0000, "TV Südostschweiz"},
   {0x2492, 0x0000, "TV Ostshweiz"},
   {0x2493, 0x0000, "Tele Ticino"},
   {0x2494, 0x0000, "Schaffhauser Fernsehen"},
   {0x2495, 0x0000, "U1 TV"},
   {0x2496, 0x0000, "MTV Swiss"},
   {0x2497, 0x0000, "3+"},
   {0x2498, 0x0000, "telebasel"},
   {0x2499, 0x0000, "NICK"},
   {0x249A, 0x0000, "SFF, Schweizer Sportfernsehen"},
   {0x249B, 0x0000, "TELE-1"},
   {0x249C, 0x0000, "TF1 Suisse"},
   {0x249D, 0x0000, "4+"},
   {0x24C1, 0x4101, "SRG, Schweizer Fernsehen DRS, SF 1"},
   {0x24C2, 0x4102, "SSR, Télévision Suisse Romande, TSR 1"},
   {0x24C3, 0x4103, "SSR, Televisione svizzera di lingua italiana, TSI 1"},
   {0x24C7, 0x4107, "SRG, Schweizer Fernsehen DRS, SF 2"},
   {0x24C8, 0x4108, "SSR, Télévision Suisse Romande, TSR 2"},
   {0x24C9, 0x4109, "SSR, Televisione svizzera di lingua italiana, TSI 2"},
   {0x24CA, 0x410A, "SRG SSR Sat Access"},
   {0x24CC, 0x0000, "SRG SSR Schweizer Fernsehen Infokanal, SFi"},
   {0x2421, 0x4121, "U1"},
   {0x2422, 0x4122, "TeleZüri"},
   {0x2400, 0x4123, "TF1 Suisse"},
   {0x2424, 0x4124, "TV24"},
   {0x2400, 0x049A, "Schweizer Sportfernsehen"},
   // Turkey
   {0x4301, 0x9001, "TRT-1"},
   {0x4302, 0x9002, "TRT-2"},
   {0x4303, 0x9003, "TRT-3"},
   {0x4304, 0x9004, "TRT-4"},
   {0x4305, 0x9005, "TRT International"},
   {0x4306, 0x9006, "AVRASYA"},
   {0x4300, 0x9007, "Show TV"},
   {0x4300, 0x9008, "Cine 5"},
   {0x4300, 0x9009, "Super Sport"},
   {0x4300, 0x900A, "ATV"},
   {0x4300, 0x900B, "KANAL D"},
   {0x4300, 0x900C, "EURO D"},
   {0x4300, 0x900D, "EKO TV"},
   {0x4300, 0x900E, "BRAVO TV"},
   {0x4300, 0x900F, "GALAKSI TV"},
   {0x4300, 0x9010, "FUN TV"},
   {0x4300, 0x9011, "TEMPO TV"},
   {0x4300, 0x9014, "TGRT"},
   {0x4300, 0x9017, "Show Euro"},
   {0x4300, 0x9020, "STAR TV"},
   {0x4300, 0x9021, "STARMAX"},
   {0x4300, 0x9022, "KANAL 6"},
   {0x4300, 0x9023, "STAR 4"},
   {0x4300, 0x9024, "STAR 5"},
   {0x4300, 0x9025, "STAR 6"},
   {0x4300, 0x9026, "STAR 7"},
   {0x4300, 0x9027, "STAR 8"},
   {0x4300, 0x90EA, "CNNT"},      // codes 90EA-90FE taken from TRT-1 Nextview
   {0x4300, 0x90EB, "NTV"},
   {0x4300, 0x90EC, "Kanal E"},
   {0x4300, 0x90ED, "Kanal 6"},
   {0x4300, 0x90EE, "STAR"},
   {0x4300, 0x90FA, "Kanal 7"},
   {0x4300, 0x90FB, "FTV"},
   {0x4300, 0x90FC, "N1"},
   {0x4300, 0x90FD, "GENC"},
   {0x4300, 0x90FE, "DISC"},
   // UK
   {0x5BF1, 0x01F2, "CNN-International"},
   {0x2C00, 0x320B, "National Geographic Channel"},
   {0x2C25, 0x37E5, "SSVC"},
   {0x5BFA, 0x4401, "UK GOLD"},
   {0x2C01, 0x4402, "UK LIVING"},
   {0x2C3C, 0x4403, "WIRE TV"},
   {0x5BF0, 0x4404, "CHILDREN'S CHANNEL"},
   {0x5BEF, 0x4405, "BRAVO"},
   {0x5BF7, 0x4406, "LEARNING CHANNEL"},
   {0x5BF2, 0x4407, "DISCOVERY"},
   {0x5BF3, 0x4408, "FAMILY CHANNEL"},
   {0x5BF8, 0x4409, "Live TV"},
   {0x5B00, 0x4420, "Discovery Home & Leisure"},
   {0x5B00, 0x4421, "Animal Planet"},
   {0x5B00, 0x44C1, "TNT / Cartoon Network"},
   {0x5BCC, 0x44D1, "DISNEY CHANNEL UK"},
   {0x2C14, 0x4D54, "MTV"},
   {0x2C20, 0x4D58, "VH-1"},
   {0x2C21, 0x4D59, "VH-1      (German language)"},
   {0x2C00, 0x5C44, "QVC UK"},
   {0x2C31, 0x8E71, "NBC Europe"},
   {0x2C35, 0x8E72, "CNBC Europe"},
   {0x2C00, 0xA460, "Nickelodeon UK"},
   {0x2C00, 0xA465, "Paramount Comedy Channel UK"},
   {0x5BD2, 0xADDC, "GMTV"},
   {0x42F4, 0xC4F4, "FilmFour"},
   {0x2C24, 0xFCE4, "CHANNEL TV"},
   {0x2C13, 0xFCF3, "RACING Ch."},
   {0x5BF6, 0xFCF4, "HISTORY Ch."},
   {0x2C15, 0xFCF5, "SCI FI CHANNEL"},
   {0x5BF9, 0xFCF6, "SKY TRAVEL"},
   {0x2C17, 0xFCF7, "SKY SOAPS"},
   {0x2C08, 0xFCF8, "SKY SPORTS 2"},
   {0x2C19, 0xFCF9, "SKY GOLD"},
   {0x2C1A, 0xFCFA, "SKY SPORTS"},
   {0x2C1B, 0xFCFB, "MOVIE CHANNEL"},
   {0x2C0C, 0xFCFC, "SKY MOVIES PLUS"},
   {0x2C0D, 0xFCFD, "SKY NEWS"},
   {0x2C0E, 0xFCFE, "SKY ONE"},
   {0x2C0F, 0xFCFF, "SKY TWO"},
   // Ukraine
   {0x77C0, 0x7700, "Studio 1+1"},
   {0x77C5, 0x7705, "M1"},
   {0x7700, 0x7707, "ICTV"},
   {0x77C8, 0x7708, "Novy Kanal"},
   // USA
   {0x0100, 0x01fa, "CNN International"},
   {0,      0,      NULL},
};

// ---------------------------------------------------------------------------
// Table to translate PDC country codes to a name
// - the country code is taken from the upper 8-bit of a PDC code
//
static const CNI_COUNTRY_DESC cni_country_table[] =
{
   {0x1A,  60, "Austria"},
   {0x16,  60, "Belgium"},
   {0x28, 120, "Bulgaria"},
   {0x46,  60, "Croatia"},
   {0x32,  60, "Czech Republic"},
   {0x29,  60, "Denmark"},
   {0x26, 120, "Finland"},
   {0x2F,  60, "France"},
   {0x1D,  60, "Germany"},
   {0x21, 120, "Greece"},
   {0x1B,  60, "Hungary"},
// {0x00,   0, "Iceland"},        // PDC country code unknown
   {0x42,   0, "Ireland"},
   {0x14, 120, "Israel"},
   {0x15,  60, "Italy"},
// {0x00,  60, "Luxembourg"},
   {0x48,  60, "Netherlands"},
   {0x3F,  60, "Norway"},
   {0x33,  60, "Poland"},
   {0x58,  60, "Portugal"},
   {0x2E, 120, "Romania"},
   {0x57, 180, "Russia"},
   {0x22,  60, "San Marino"},
   {0x35,  60, "Slovakia"},
// {0x00,  60, "Slovenia"},
   {0x3E,  60, "Spain"},
   {0x4E,  60, "Sweden"},
   {0x24,  60, "Switzerland"},
   {0x43, 120, "Turkey"},
   {0x2C,   0, "UK"},
   {0x5B,   0, "UK"},
   {0x77, 180, "Ukraine"},
   {0x01,  60, "USA"},            // for CNN-Int only, uses CET
   {0, 0, NULL},
};

// ---------------------------------------------------------------------------
// Look up the given CNI's country entry
// - given CNI must be a PDC CNI (i.e. NI must be converted to PDC before)
//
static const CNI_COUNTRY_DESC * CniSearchCountryTable( uint pdcCni )
{
   const CNI_COUNTRY_DESC * pCountry;
   uint country;

   pCountry = cni_country_table;
   country  = pdcCni >> 8;

   while (pCountry->name != NULL)
   {
      if (pCountry->code == country)
      {
         break;
      }
      pCountry += 1;
   }

   if (pCountry->name == NULL)
   {
      pCountry = NULL;
   }
   return pCountry;
}

// ---------------------------------------------------------------------------
// Convert VPS codes to PDC
// - VPS originally allowed only 4 bits country code; although it supports
//   8 bits today, all networks still transmit only 4 bits
//
static uint CniConvertVpsToPdc( uint vpsCni )
{
   uint pdcCni;

   switch (vpsCni >> 8)
   {
      case 0x0D:  // Germany
      case 0x0A:  // Austria
         pdcCni = vpsCni | 0x1000;
         break;

      case 0x04:  // Switzerland
         pdcCni = vpsCni | 0x2000;
         break;

      default:
         pdcCni = vpsCni;
         break;
   }
   return pdcCni;
}

// ---------------------------------------------------------------------------
// Look up the given CNI's table entry
// - given CNI may be either PDC or P8/30/1 CNI
//
static const CNI_PDC_DESC * CniSearchNiPdcTable( uint cni )
{
   const CNI_PDC_DESC * pPdc;
   uint  pdcCni;

   pPdc = cni_pdc_desc_table;

   // convert VPS codes to PDC
   pdcCni = CniConvertVpsToPdc(cni);

   while (pPdc->name != NULL)
   {
      if ( (pPdc->pdc == pdcCni) || (pPdc->ni == cni) )
      {
         break;
      }
      pPdc += 1;
   }

   if (pPdc->name == NULL)
   {
      pPdc = NULL;
   }
   return pPdc;
}

// ---------------------------------------------------------------------------
// Search a description for the given CNI
// - returns a pointer to a channel label string, or NULL for unknown CNIs
// - also returns pointer to country string, or NULL if unknown
//
const char * CniGetDescription( uint cni, const char ** ppCountry )
{
   const CNI_PDC_DESC * pPdc;
   const CNI_COUNTRY_DESC * pCountry;
   const char * pName;
   const char * pCountryName;

   pName = NULL;
   pCountryName = NULL;

   if (cni == 0)
   {
      debug0("Cni-GetDescription: illegal CNI 0");
   }
   else if ((cni & 0xff00) == 0xff00)
   {
      pName = "unknown network (temporary network code, not officially registered)";
   }
   else
   {
      // search PDC/NI table for the given code
      pPdc = CniSearchNiPdcTable(cni);
      if (pPdc != NULL)
      {
         pName = pPdc->name;

         pCountry = CniSearchCountryTable(pPdc->pdc);
         if (pCountry != NULL)
         {
            pCountryName = pCountry->name;
         }
      }
   }

   if (ppCountry != NULL)
      *ppCountry = pCountryName;

   return pName;
}

// ---------------------------------------------------------------------------
// Retrieve LTO for given provider
//
bool CniGetProviderLto( uint cni, sint * pLto )
{
   const CNI_PDC_DESC * pPdc;
   const CNI_COUNTRY_DESC * pCountry;
   bool result = FALSE;

   if (pLto != NULL)
   {
      if ( (cni != 0) && ((cni & 0xff00) != 0xff00) )
      {
         // search PDC/NI table to map NI codes to PDC
         pPdc = CniSearchNiPdcTable(cni);
         if (pPdc != NULL)
         {
            pCountry = CniSearchCountryTable(pPdc->pdc);
            if (pCountry->name != NULL)
            {
               *pLto = pCountry->lto * 60;
               result = TRUE;
            }
         }
      }
      else
         debug1("Cni-GetProviderLto: invalid CNI 0x%04X\n", cni);
   }
   else
      debug0("Cni-GetProviderLto: illegal NULL ptr param\n");

   return result;
}

// ---------------------------------------------------------------------------
// Convert Packet 8/30/1 CNI to VPS value
// - some networks have both a VPS identification code and a (different)
//   NI (packet 8/30/1) code. Unfortunately in Nextview only the bare values
//   are transmitted without information about which table they are from.
// - to avoid dealing with multiple CNIs per network in the upper layers
//   all codes are converted to VPS when possible. This conforms to the
//   current practice of Nextview providers.
//
uint CniConvertP8301ToVps( uint cni )
{
   const CNI_PDC_DESC * pPdc;

   if ((cni != 0) && (cni != 0xffff))
   {
      // search PDC/NI table for the given code
      for (pPdc = cni_pdc_desc_table; pPdc->pdc != 0; pPdc += 1)
      {
         if (pPdc->ni == cni)
         {
            if ((pPdc->pdc & 0xff) != 0)
            {  // PDC code available -> replace NI with it
               cni = pPdc->pdc;

               // convert PDC codes to old 12-bit VPS code
               switch (cni >> 8)
               {
                  case 0x1D:
                  case 0x1A:
                  case 0x24:
                  case 0x77:
                     cni &= 0x0FFF;
                     break;
               }
            }
            // terminate the loop over the PDC table
            break;
         }
      }
   }

   return cni;
}

// ---------------------------------------------------------------------------
// Convert Packet 8/30/2 CNI to VPS
// - PDC is equivalent to VPS, except that is uses 16 bit and VPS only 12
//   (note: later versions of the VPS standard allow 16 bit too, but currently
//   no networks use this possibility, so the upper 4 bits are always 0)
// - currently all Nextview providers use 12 bit VPS codes for networks which
//   transmit VPS, so it's the easiest solution to convert to 12bit VPS.
//
uint CniConvertPdcToVps( uint cni )
{
   switch (cni >> 8)
   {
      case 0x1D:  // country code for Germany
      case 0xFD:
      case 0x1A:  // country code for Autria
      case 0xFA:
      case 0x24:  // country code for Switzerland
      case 0xF4:
      case 0x77:  // country code for Ukraine
      case 0xF7:
         // discard the upper 4 bits of the country code
         cni &= 0x0fff;
         break;

      default:
         break;
   }
   return cni;
}

// ---------------------------------------------------------------------------
// Convert a CNI of unknown type into PDC
// - CNIs which originate from AI blocks may have any type
//   for comparison with "live" CNIs they need to be converted into PDC
//
uint CniConvertUnknownToPdc( uint cni )
{
   uint pdcCni;

   pdcCni = CniConvertP8301ToVps(cni);
   if (pdcCni == cni)
   {
      //pdcCni = CniConvertPdcToVps(cni);
      switch (pdcCni >> 8)
      {
         case 0x1D:  // country code for Germany
         case 0x1A:  // country code for Autria
         case 0x24:  // country code for Switzerland
         case 0x77:  // country code for Ukraine
            // discard the upper 4 bits of the country code
            pdcCni &= 0x0fff;
            break;

         default:
            break;
      }
   }
   return pdcCni;
}
