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
 *  $Id: cni_tables.c,v 1.23 2004/03/21 18:07:31 tom Exp $
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
   {0x1ACB, 0x0000, "ORF- FS 2: Lokalprogramm Burgenland"},
   {0x1ACC, 0x0000, "ORF- FS 2: Lokalprogramm Kärnten"},
   {0x1ACD, 0x0000, "ORF- FS 2: Lokalprogramm Niederösterreich"},
   {0x1ACE, 0x0000, "ORF- FS 2: Lokalprogramm Oberösterreich"},
   {0x1ACF, 0x0000, "ORF- FS 2: Lokalprogramm Salzburg"},
   {0x1AD0, 0x0000, "ORF- FS 2: Lokalprogramm Steiermark"},
   {0x1AD1, 0x0000, "ORF- FS 2: Lokalprogramm Tirol"},
   {0x1AD2, 0x0000, "ORF- FS 2: Lokalprogramm Vorarlberg"},
   {0x1AD3, 0x0000, "ORF- FS 2: Lokalprogramm Wien"},
   // Belgium
   {0x1601, 0x3201, "VRT TV1"},
   {0x1602, 0x3202, "CANVAS"},
   {0x1600, 0x3203, "RTBF 1"},
   {0x1600, 0x3204, "RTBF 2"},
   {0x1605, 0x3205, "VTM"},
   {0x1606, 0x3206, "Kanaal2"},
   {0x1600, 0x3207, "RTBF Sat"},
   {0x1600, 0x3209, "RTL-TVI"},
   {0x1600, 0x320A, "CLUB-RTL"},
   {0x1600, 0x320C, "AB3"},
   {0x1600, 0x320F, "JIM.tv"},
   {0x1604, 0x0404, "VT4"},
   // Croatia
   {0x4600, 0x0385, "HRT"},
   // Czech Republic
   {0x32C1, 0x4201, "CT 1"},
   {0x32C2, 0x4202, "CT 2"},
   {0x32C3, 0x4203, "NOVA TV"},
   {0x32D1, 0x4211, "CT1 Regional, Brno"},
   {0x32D2, 0x4212, "CT2 Regional, Brno"},
   {0x32E1, 0x4221, "CT1 Regional, Ostravia"},
   {0x32E2, 0x4222, "CT2 Regional, Ostravia"},
   {0x32F1, 0x4231, "CT1 Regional"},
   {0x32F2, 0x4232, "CT2 Regional"},
   // Denmark
   {0x2901, 0x7392, "DR1"},
   {0x2902, 0x4502, "TV2"},
   {0x2903, 0x49CF, "DR2"},
   {0x2904, 0x4503, "TV2 Zulu"},
   {0x2900, 0x4504, "Discovery"},
   // Finland
   {0x260F, 0x358F, "OWL3"},
   {0x2601, 0x3581, "YLE1"},
   {0x2602, 0x3582, "YLE2"},
   // France
   {0x2FC1, 0x33C1, "AB1"},
   {0x2F20, 0x3320, "Aqui TV"},
   {0x2F0A, 0x330A, "Arte"},
   {0x2FC2, 0x33C2, "Canal J"},
   {0x2FC3, 0x33C3, "Canal Jimmy"},
   {0x2F04, 0x33F4, "Canal+"},
   {0x2FE1, 0xFE01, "Euronews"},
   {0x2FE2, 0xF101, "Eurosport"},
   {0x2F01, 0x33F1, "France 1 (TF1)"},
   {0x2F02, 0x33F2, "France 2"},
   {0x2F03, 0x33F3, "France 3"},
   {0x2F05, 0x33F5, "France 5 (La Cinquième)"},
   {0x2FC5, 0x33C5, "La Chaîne Météo"},
   {0x2FC4, 0x33C4, "LCI"},
   {0x2F06, 0x33F6, "M6"},
   {0x2FC6, 0x33C6, "MCM"},
   {0x2FC8, 0x33C8, "Paris Première"},
   {0x2FC9, 0x33C9, "Planète"},
   {0x2F11, 0x3311, "RFO1"},
   {0x2F12, 0x3312, "RFO2"},
   {0x2F00, 0x33B2, "Sailing Channel"},
   {0x2FCA, 0x33CA, "Série Club"},
   {0x2FCB, 0x33CB, "Télétoon"},
   {0x2FCC, 0x33CC, "Téva"},
   {0x2F21, 0x3321, "TLM"},
   {0x2F22, 0x3322, "TLT"},
   {0x2FC7, 0x33C7, "TMC Monte-Carlo"},
   {0x2FE5, 0xF500, "TV5"},
   // Germany
   {0x1D41, 0x4941, "Festival"},
   {0x1D42, 0x4942, "MUXX"},
   {0x1D43, 0x4943, "EXTRA"},
   {0x1D44, 0x4944, "BR-Alpha: Bildungskanal des Bayerischen Rundfunks"},
   {0x1D7A, 0x0000, "n24"},
   {0x1D7B, 0x49BE, "Tele-5"},
   {0x1D7C, 0x0000, "ONYX-TV"},
   {0x1D7D, 0x5C49, "QVC-Teleshopping"},
   {0x1D7E, 0x0000, "Nickelodeon"},
   {0x1D7F, 0x49BF, "Home Shopping Europe"},
   {0x1D81, 0x0000, "ORB-1: Regionalprogramm"},
   {0x1D82, 0x4982, "ORB-3: Ostdeutscher Rundfunk Brandenburg"},
   {0x1D85, 0x490A, "Arte"},
   {0x1D87, 0x0000, "1A-Fernsehen"},
   {0x1D88, 0x0000, "VIVA"},
   {0x1D89, 0x0000, "VIVA 2"},
   {0x1D8A, 0x0000, "Super RTL"},
   {0x1D8B, 0x0000, "RTL Club"},
   {0x1D8C, 0x0000, "n-tv"},
   {0x1D8D, 0x0000, "Deutsches Sportfernsehen"},
   {0x1D8E, 0x490C, "VOX Television"},
   {0x1D8F, 0x0000, "RTL 2"},
   {0x1D90, 0x0000, "RTL 2: regional"},
   {0x1D91, 0x0000, "Eurosport"},
   {0x1D92, 0x0000, "Kabel 1"},
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
   {0x1DAB, 0x0000, "RTL Plus"},
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
   {0x1DBA, 0x0000, "9live (former tm-3)"},
   {0x1DBB, 0x0000, "Deutsche Welle Fernsehen Berlin"},
   {0x1DBD, 0x0000, "Berlin-Offener Kanal"},
   {0x1DBE, 0x0000, "Berlin-Mix-Channel II"},
   {0x1DBF, 0x0000, "Berlin-Mix-Channel I"},
   {0x1DC1, 0x4901, "ARD: Erstes Deutsches Fernsehen"},
   {0x1DC2, 0x4902, "ZDF: Zweites Deutsches Fernsehen"},
   {0x1DC7, 0xD107, "3sat (ARD/ZDF/ORF/SRG common programme)"},  // 0x49C7 in ETSI TR
   {0x1DC8, 0x4918, "Phoenix: Ereignis und Dokumentationskanal (ARD/ZDF)"},  // 0x4908 in ETSI TR
   {0x1DC9, 0x49C9, "Kinderkanal (ARD/ZDF)"},
   {0x1DCA, 0x0000, "BR-1: Regionalprogramm"},
   {0x1DCB, 0x49CB, "BR-3: Bayerischer Rundfunk"},
   {0x1DCC, 0x0000, "BR-3: Süd"},
   {0x1DCD, 0x0000, "BR-3: Nord"},
   {0x1DCE, 0x49FF, "HR-1: Regionalprogramm"},
   {0x1DCF, 0x4915, "HR-3: Hessischer Rundfunk"},
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
   {0x1B00, 0x3636, "Duna Televizio"},
   {0x1B00, 0x3601, "MTV1"},
   {0x1B00, 0x3611, "MTV1 regional, Budapest"},
   {0x1B00, 0x3651, "MTV1 regional, Debrecen"},
   {0x1B00, 0x3661, "MTV1 regional, Miskolc"},
   {0x1B00, 0x3621, "MTV1 regional, Pécs"},
   {0x1B00, 0x3631, "MTV1 regional, Szeged"},
   {0x1B00, 0x3641, "MTV1 regional, Szombathely"},
   {0x1B00, 0x3602, "MTV2"},
   {0x1B00, 0x3622, "tv2"},
   // Iceland
   {0x4200, 0x3541, "Rikisutvarpid-Sjonvarp"},
   // Ireland
   {0x4202, 0x3532, "Network 2"},
   {0x4201, 0x3531, "RTE1"},
   {0x4203, 0x3533, "Teilifis na Gaeilge"},
   {0x4200, 0x3333, "TV3"},
   // Italy
   {0x1500, 0x390A, "Arte"},
   {0x1500, 0xFA05, "Canale 5"},
   {0x1500, 0x3939, "GAY TV"},
   {0x1500, 0xFA06, "Italia 1"},
   {0x1500, 0x3933, "MTV Italia"},
   {0x1500, 0x3901, "RAI 1"},
   {0x1500, 0x3902, "RAI 2"},
   {0x1500, 0x3903, "RAI 3"},
   {0x1500, 0xFA04, "Rete 4"},
   {0x1500, 0x3904, "Rete A"},
   {0x1500, 0x3920, "RaiNews24"},
   {0x1500, 0x3921, "Rai Med"},
   {0x1500, 0x3922, "Rai Sport"},
   {0x1500, 0x3923, "Rai Educational"},
   {0x1500, 0x3924, "Rai Edu Lab"},
   {0x1500, 0x3925, "Rai Nettuno 1"},
   {0x1500, 0x3926, "Rai Nettuno 2"},
   {0x1500, 0x3927, "Camera Deputati"},
   {0x1500, 0x3928, "Rai Mosaico"},
   {0x1500, 0x3950, "RaiSat Album"},
   {0x1500, 0x3951, "RaiSat Art"},
   {0x1500, 0x3952, "RaiSat Cinema"},
   {0x1500, 0x3953, "RaiSat Fiction"},
   {0x1500, 0x3954, "RaiSat GamberoRosso channel"},
   {0x1500, 0x3955, "RaiSat Ragazzi"},
   {0x1500, 0x3956, "RaiSat Show"},
   {0x1500, 0x3957, "RaiSat G. Rosso interattivo"},
   {0x1500, 0x3938, "RTV38"},
   {0x1500, 0x39B1, "Sailing Channel"},
   {0x1500, 0x3997, "Tele+1"},
   {0x1500, 0x3998, "Tele+2"},
   {0x1500, 0x3999, "Tele+3"},
   {0x1500, 0xFA08, "TMC"},
   {0x1500, 0x3910, "TRS TV"},
   {0x1500, 0x3940, "Video Italia"},
   // Luxembourg
   {0x0000, 0x4000, "RTL Télé Lëtzebuerg"},
   // Netherlands
   {0x4801, 0x3101, "Nederland 1"},
   {0x4802, 0x3102, "Nederland 2"},
   {0x4803, 0x3103, "Nederland 3"},
   {0x4804, 0x3104, "RTL 4"},
   {0x4805, 0x3105, "RTL 5"},
   {0x4806, 0x3106, "Yorin"},
   {0x4820, 0x3120, "The BOX"},
   {0x4800, 0x3121, "Discovery Netherlands"},
   {0x4822, 0x3122, "Kindernet/Veronica"},
   {0x4800, 0x3125, "NET5"},
   {0x4800, 0x3126, "SBS6"},
   {0x4800, 0x3128, "V8"},
   // Norway
   {0x3F00, 0x4701, "NRK1"},
   {0x3F00, 0x4703, "NRK2"},
   {0x3F00, 0x4702, "TV 2"},
   {0x3F00, 0x4704, "TV Norge"},
   {0x3F00, 0x4720, "Discovery Nordic"},
   // Poland
   {0x3300, 0x4810, "TV Polonia"},
   {0x3300, 0x4801, "TVP1"},
   {0x3300, 0x4802, "TVP2"},
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
   {0x3300, 0x4820, "TVN"},
   {0x3300, 0x4821, "TVN Siedem"},
   {0x3300, 0x4822, "TVN24"},
   // Portugal
   {0x5800, 0x3510, "RTP1"},
   {0x5800, 0x3511, "RTP2"},
   {0x5800, 0x3512, "RTPAF"},
   {0x5800, 0x3514, "RTPAZ"},
   {0x5800, 0x3513, "RTPI"},
   {0x5800, 0x3515, "RTPM"},
   // San Marino
   {0x2200, 0x3781, "RTV"},
   // Slovakia
   {0x35A1, 0x42A1, "STV1"},
   {0x35A2, 0x42A2, "STV2"},
   {0x35A3, 0x42A3, "STV1 Regional, Kosice"},
   {0x35A4, 0x42A4, "STV2 Regional, Kosice"},
   {0x35A5, 0x42A5, "STV1 Regional, B. Bystrica"},
   {0x35A6, 0x42A6, "STV2 Regional, B. Bystrica"},
   // Slovenia
   {0x0000, 0xAAE3, "KC"},
   {0x0000, 0xAAE1, "SLO1"},
   {0x0000, 0xAAE2, "SLO2"},
   {0x0000, 0xAAE4, "TLM"},
   {0x0000, 0xAAF1, "SLO3"},
   // Spain
   {0x3E00, 0x340A, "Arte"},
   {0x3E00, 0xCA33, "C33"},
   {0x3E00, 0xBA01, "ETB 1"},
   {0x3E00, 0x3402, "ETB 2"},
   {0x3E00, 0xCA03, "TV3"},
   {0x3E00, 0x3E00, "TVE1"},
   {0x3E00, 0xE100, "TVE2"},
   // Sweden
   {0x4E01, 0x4601, "SVT 1"},
   {0x4E02, 0x4602, "SVT 2"},
   {0x4E40, 0x4640, "TV 4"},
   // Switzerland
   {0x2481, 0x0000, "TeleZüri"},
   {0x2482, 0x0000, "Teleclub Abonnements-Fernsehen"},
   {0x2484, 0x0000, "TeleBern"},
   {0x2485, 0x0000, "Tele M1"},
   {0x2486, 0x0000, "Star TV"},
   {0x2487, 0x0000, "Pro Sieben"},
   {0x2488, 0x0000, "TopTV"},
   {0x24C1, 0x4101, "SRG, Schweizer Fernsehen DRS, SF 1"},
   {0x24C2, 0x4102, "SSR, Télévision Suisse Romande, TSR 1"},
   {0x24C3, 0x4103, "SSR, Televisione svizzera di lingua italiana, TSI 1"},
   {0x24C7, 0x4107, "SRG, Schweizer Fernsehen DRS, SF 2"},
   {0x24C8, 0x4108, "SSR, Télévision Suisse Romande, TSR 2"},
   {0x24C9, 0x4109, "SSR, Televisione svizzera di lingua italiana, TSI 2"},
   {0x24CA, 0x410A, "SRG SSR Sat Access"},
   // Turkey
   {0x4301, 0x9001, "TRT-1"},
   {0x4302, 0x9002, "TRT-2"},
   {0x4303, 0x9003, "TRT-3"},
   {0x4304, 0x9004, "TRT-4"},
   {0x4305, 0x9005, "TRT-int"},
   {0x4306, 0x9006, "AVRASYA"},
   {0x4300, 0x9007, "Show TV"},
   {0x4300, 0x9008, "Cine 5"},
   {0x4300, 0x9009, "Super Sport"},
   {0x4300, 0x900A, "ATV"},
   {0x4300, 0x900B, "Kanal D"},
   {0x4300, 0x900E, "BRAVO TV"},
   {0x4300, 0x900D, "EKO TV"},
   {0x4300, 0x900C, "EURO D"},
   {0x4300, 0x900F, "GALAKSI TV"},
   {0x4300, 0x9010, "FUN TV"},
   {0x4300, 0x9011, "TEMPO TV"},
   {0x4300, 0x9014, "TGRT"},
   {0x4300, 0x9020, "STAR TV"},
   {0x4300, 0x9021, "STARMAX"},
   {0x4300, 0x9022, "Kanal 6"},
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
   {0x2C1C, 0xFB9C, "ANGLIA TV"},
   {0x2C69, 0x4469, "BBC News 24"},
   {0x2C68, 0x4468, "BBC Prime"},
   {0x2C57, 0x4457, "BBC World"},
   {0x2C7F, 0x447F, "BBC1"},
   {0x2C41, 0x4441, "BBC1 NI"},
   {0x2C7B, 0x447B, "BBC1 Scotland"},
   {0x2C7D, 0x447D, "BBC1 Wales"},
   {0x2C40, 0x4440, "BBC2"},
   {0x2C7E, 0x447E, "BBC2 NI"},
   {0x2C44, 0x4444, "BBC2 Scotland"},
   {0x2C42, 0x4442, "BBC2 Wales"},
   {0x2C2F, 0xFA6F, "BBC1 (obsolete CNI code)"},  // obsolete, but still in use (in Belgium)
   {0x2C3E, 0xA2FE, "BBC2 (obsolete CNI code)"},
   {0x2C04, 0x1984, "BBC (obsolete CNI code)"},
   {0x2C0A, 0x200A, "BBC (obsolete CNI code)"},
   {0x2C39, 0x3F39, "BBC (obsolete CNI code)"},
   {0x2C27, 0xB7F7, "BORDER TV"},
   {0x5BEF, 0x4405, "BRAVO"},
   {0x2C05, 0x82E1, "CARLTON SELECT"},
   {0x2C1D, 0x82DD, "CARLTON TV"},
   {0x2C37, 0x2F27, "CENTRAL TV"},
   {0x2C11, 0xFCD1, "CHANNEL 4"},
   {0x2C02, 0x9602, "CHANNEL 5 (1)"},
   {0x2C09, 0x1609, "CHANNEL 5 (2)"},
   {0x2C2B, 0x28EB, "CHANNEL 5 (3)"},
   {0x2C3B, 0xC47B, "CHANNEL 5 (4)"},
   {0x2C24, 0xFCE4, "CHANNEL TV"},
   {0x5BF0, 0x4404, "CHILDREN'S CHANNEL"},
   {0x5BF1, 0x01F2, "CNN international"},
   {0x5BF2, 0x4407, "DISCOVERY"},
   {0x5BF3, 0x4408, "FAMILY CHANNEL"},
   {0x5BD2, 0xADDC, "GMTV"},
   {0x2C3A, 0xF33A, "GRAMPIAN TV"},
   {0x5BF4, 0x4D5A, "GRANADA PLUS"},
   {0x5BF5, 0x4D5B, "GRANADA Timeshare"},
   {0x2C18, 0xADD8, "GRANADA TV"},
   {0x5BF6, 0xFCF4, "HISTORY Channel"},
   {0x2C3F, 0x5AAF, "HTV"},
   {0x2C1E, 0xC8DE, "ITV NETWORK"},
   {0x5BF7, 0x4406, "LEARNING CHANNEL"},
   {0x5BF8, 0x4409, "Live TV"},
   {0x2C0B, 0x884B, "LWT"},
   {0x2C34, 0x10E4, "MERIDIAN"},
   {0x2C1B, 0xFCFB, "MOVIE CHANNEL"},
   {0x2C14, 0x4D54, "MTV"},
   {0x2C31, 0x8E71, "NBC Europe"},
   {0x2C35, 0x8E72, "CNBC Europe"},
   {0x2C00, 0xA460, "Nickelodeon UK"},
   {0x2C00, 0xA465, "Paramount Comedy Channel UK"},
   {0x2C00, 0x5C44, "QVC UK"},
   {0x2C13, 0xFCF3, "RACING Channel"},
   {0x2C07, 0xB4C7, "S4C"},
   {0x2C15, 0xFCF5, "SCI FI CHANNEL"},
   {0x2C12, 0xF9D2, "SCOTTISH TV"},
   {0x2C19, 0xFCF9, "SKY GOLD"},
   {0x2C0C, 0xFCFC, "SKY MOVIES PLUS"},
   {0x2C0D, 0xFCFD, "SKY NEWS"},
   {0x2C0E, 0xFCFE, "SKY ONE"},
   {0x2C17, 0xFCF7, "SKY SOAPS"},
   {0x2C1A, 0xFCFA, "SKY SPORTS"},
   {0x2C08, 0xFCF8, "SKY SPORTS 2"},
   {0x5BF9, 0xFCF6, "SKY TRAVEL"},
   {0x2C0F, 0xFCFF, "SKY TWO"},
   {0x2C25, 0x37E5, "SSVC"},
   {0x2C2C, 0xA82C, "TYNE TEES TV"},
   {0x5BFA, 0x4401, "UK GOLD"},
   {0x2C01, 0x4402, "LIVING"},
   {0x2C3D, 0x833B, "ULSTER TV"},
   {0x2C20, 0x4D58, "VH-1"},
   {0x2C21, 0x4D59, "VH-1 Germany"},
   {0x2C30, 0x25D0, "WESTCOUNTRY TV"},
   {0x2C3C, 0x4403, "WIRE TV"},
   {0x2C2D, 0xFA2C, "YORKSHIRE TV"},
   // Ukraine
   {0x7700, 0x7700, "1+1"},
   {0x7700, 0x7705, "M1"},
   // USA
   {0x0100, 0x01fa, "CNN International"},
   {0,      0,      NULL},
};

// ---------------------------------------------------------------------------
// Table to translate PDC country codes to a name
// - the country code is taken from the upper 16-bit of a PDC code
//
static const CNI_COUNTRY_DESC cni_country_table[] =
{
   {0x1A, "Austria"},
   {0x16, "Belgium"},
   {0x28, "Bulgaria"},
   {0x46, "Croatia"},
   {0x32, "Czech Republic"},
   {0x29, "Denmark"},
   {0x26, "Finland"},
   {0x2F, "France"},
   {0x1D, "Germany"},
   {0x21, "Greece"},
   {0x1B, "Hungary"},
   //{0x00, "Iceland"},        // PDC country code unknown
   {0x42, "Ireland"},
   {0x14, "Israel"},
   {0x15, "Italy"},
   //{0x00, "Luxembourg"},
   {0x48, "Netherlands"},
   {0x3F, "Norway"},
   {0x33, "Poland"},
   {0x58, "Portugal"},
   {0x2E, "Romania"},
   {0x57, "Russia"},
   {0x22, "San Marino"},
   {0x35, "Slovakia"},
   //{0x00, "Slovenia"},
   {0x3E, "Spain"},
   {0x4E, "Sweden"},
   {0x24, "Switzerland"},
   {0x43, "Turkey"},
   {0x2C, "UK"},
   {0x5B, "UK"},
   //{0x00, "Ukraine"},
   {0x01, "USA"},
   {0, NULL},
};

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
   uchar country;

   if (cni == 0)
   {
      debug0("Cni-GetDescription: illegal CNI 0");
      pName = NULL;
      pCountryName = NULL;
   }
   else if ((cni & 0xff00) == 0xff00)
   {
      pName = "unknown network (temporary network code, not officially registered)";
      pCountryName = NULL;
   }
   else
   {
      // convert VPS codes to PDC
      switch (cni >> 8)
      {
         case 0x0D:
         case 0x0A:
            cni |= 0x1000;
            break;
         case 0x04:
            cni |= 0x2000;
            break;
         default:
            break;
      }

      // search PDC/NI table for the given code
      pName = NULL;
      country = 0;
      pPdc = cni_pdc_desc_table;

      while (pPdc->name != NULL)
      {
         if ( (pPdc->pdc == cni) || (pPdc->ni == cni) )
         {
            pName   = pPdc->name;
            country = pPdc->pdc >> 8;
            break;
         }
         pPdc += 1;
      }

      // search country name
      pCountryName = NULL;
      if (country != 0)
      {
         pCountry = cni_country_table;
         while (pCountry->name != NULL)
         {
            if (pCountry->code == country)
            {
               pCountryName = pCountry->name;
               break;
            }
            pCountry += 1;
         }
      }
   }

   if (ppCountry != NULL)
      *ppCountry = pCountryName;

   return pName;
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
         // discard the upper 4 bits of the country code
         cni &= 0x0fff;
         break;

      default:
         break;
   }
   return cni;
}

// ---------------------------------------------------------------------------
// List of networks who are known to transmit Nextview
//
static const uint cni_prov_table[] =
{
   0x0D8F,  // RTL-II (Germany)
   0x1D8F,
   0x0D92,  // Kabel1 (Germany)
   0x1D92,
   //0x0D94,  // PRO7 (Germany) - deceased Apr/14/2002
   //0x1D94,
   //0x0DC7,  // 3SAT (Germany) - deceased May/31/2003
   //0x1DC7,
   0x2FE1,  // EuroNews (Germany)
   0xFE01,
   0x24C1,  // SF1 (Switzerland)
   0x4101,
   0x24C2,  // TSR1 (Switzerland)
   0x4102,
   0x24C3,  // TSI1 (Switzerland)
   0x4103,
   0x2FE5,  // TV5 (France)
   0xF500,
   0x2F04,  // Canal+ (France)
   0x33F4,
   0x2F06,  // M6 (France)
   0x1604,  // VT4 (Belgium)
   0x0404,
   0x9001,  // TRT-1 (Turkey)
   0
};

// ---------------------------------------------------------------------------
// Check if the given CNI belongs to a known Nextview provider
//
bool CniIsKnownProvider( uint cni )
{
   const uint * pCni;
   bool result = FALSE;

   if (cni != 0)
   {
      for (pCni = cni_prov_table; *pCni != 0; pCni++)
      {
         if (*pCni == cni)
         {
            result = TRUE;
            break;
         }
      }
   }

   return result;
}

