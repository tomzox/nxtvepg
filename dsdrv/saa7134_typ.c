/*
 *  Philips SAA7134 card types list
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
 *    This module contains a list of TV cards based on the Philips SAA7134
 *    chip family.  If offers functions to autodetect cards (based on their
 *    PCI subsystem ID which can be stored in the EEPROM on the card) and
 *    tuners (for certain vendors only, if type is stored in EEPROM),
 *    enumerate card and input lists and card-specific functions to switch
 *    the video input channel.
 *
 *
 *  Authors:
 *
 *    Copyright (c) 2002 Atsushi Nakagawa.  All rights reserved.
 *
 *  DScaler #Id: SAA7134Card_Types.cpp,v 1.46 2004/03/26 14:17:52 atnak Exp #
 *
 *  $Id: saa7134_typ.c,v 1.17 2004/05/22 19:50:01 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_DSDRV
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "dsdrv/dsdrvlib.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/wintuner.h"
#include "dsdrv/saa7134_reg.h"
#include "dsdrv/saa7134_typ.h"


/// Different types of input currently supported
typedef enum
{
    /// standard composite input
    INPUTTYPE_COMPOSITE,
    /// standard s-video input
    INPUTTYPE_SVIDEO,
    /// standard analogue tuner input composite
    INPUTTYPE_TUNER,
    /// Digital CCIR656 input on the GPIO pins
    INPUTTYPE_CCIR,
    /// Radio input so no video
    INPUTTYPE_RADIO,
    /// When the card doesn't have internal mute
    INPUTTYPE_MUTE,
    /// Stores the state the cards should be put into at the end
    //INPUTTYPE_FINAL,
} eInputType;

/// SAA7134's video input pins
typedef enum 
{
    VIDEOINPUTSOURCE_NONE = -1,     // reserved for radio
    VIDEOINPUTSOURCE_PIN0 = 0,
    VIDEOINPUTSOURCE_PIN1,
    VIDEOINPUTSOURCE_PIN2,
    VIDEOINPUTSOURCE_PIN3,
    VIDEOINPUTSOURCE_PIN4,
    VIDEOINPUTSOURCE_PIN5,
} eVideoInputSource;

/// Possible clock crystals a card could have
typedef enum
{
    AUDIOCRYSTAL_NONE = 0,          // only on saa7130
    AUDIOCRYSTAL_32110Hz,
    AUDIOCRYSTAL_24576Hz,
} eAudioCrystal;

typedef enum
{
    /// standard tuner line - 0x02
    AUDIOINPUTSOURCE_DAC = 0,
    /// internal line 1 input - 0x00
    AUDIOINPUTSOURCE_LINE1,
    /// internal line 2 input - 0x01
    AUDIOINPUTSOURCE_LINE2,
} eAudioInputSource;

/// Defines each input on a card
typedef struct
{
    /// Name of the input
    LPCSTR szName;
    /// Type of the input
    eInputType InputType;
    /// Which video pin on the card is to be used
    eVideoInputSource VideoInputPin;
    /// Which line on the card is to be default
    eAudioInputSource AudioLineSelect;
    DWORD dwGPIOStatusMask;
    DWORD dwGPIOStatusBits;
} TInputType;
     
/// Defines the specific settings for a given card
#define INPUTS_PER_CARD 7
typedef struct
{
    LPCSTR szName;
    WORD DeviceId;
    int NumInputs;
    TInputType Inputs[INPUTS_PER_CARD];
    eTunerId TunerId;
    /// The type of clock crystal the card has
    eAudioCrystal AudioCrystal;
    DWORD dwGPIOMode;
    /// Any card specific initialization - may be NULL
    void (*pInitCardFunction)(void);
    /** Function used to switch between sources
        Cannot be NULL
        Default is StandardBT848InputSelect
    */
    void (*pInputSwitchFunction)(TVCARD * pTvCard, int nInput);
    DWORD dwAutoDetectId;
} TCardType;

static void StandardSAA7134InputSelect(TVCARD * pTvCard, int nInput);

static const TCardType m_SAA7134Cards[] =
{
    // SAA7134CARDID_UNKNOWN - Unknown Card
    {
        "*Unknown Card*",
        0x0000,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
        },
        TUNER_ABSENT,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_PROTEUSPRO - Proteus Pro [philips reference design]
    {
        "Proteus Pro [philips reference design]",
        0x7134,
        2,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x20011131,
    },
    // LifeView FlyVideo 3000
    // Chronos Video Shuttle II (Based on FlyVideo 3000, Stereo)
    // Thanks "Velizar Velinov" <veli_velinov2001@ya...>
    {
        "LifeView FlyVideo3000 / Chronos Video Shuttle II",
        0x7134,
        6,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
                //0xE000, 0x8000,
                0xE000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
                //0xE000, 0x0000,
                0xE000, 0x2000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0xE000, 0x8000,
            },
            #endif
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        0x0018e700,
        NULL,
        StandardSAA7134InputSelect,
        0x01384e42,
    },
    // LifeView FlyVideo2000 (saa7130)
    // Chronos Video Shuttle II (Based on FlyVideo 2000)
    {
        "LifeView FlyVideo2000 / Chronos Video Shuttle II",
        0x7130,
        6,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x2000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0xE000, 0x8000,
            },
            #endif
        },
        TUNER_LG_TAPCNEW_PAL,
        AUDIOCRYSTAL_NONE,
        0x0018e700,
        NULL,
        StandardSAA7134InputSelect,
        0x01385168,
    },
    // SAA7134CARDID_EMPRESS - EMPRESS (has TS, i2srate=48000, has CCIR656 video out)
    {
        "EMPRESS",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x67521131,
    },
    // SAA7134CARDID_MONSTERTV - SKNet Monster TV
    {
        "SKNet Monster TV",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_MK2_NTSC,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x4E851131,
    },
    // SAA7134CARDID_TEVIONMD9717 - Tevion MD 9717
    {
        "Tevion MD 9717",
        0x7134,
        5,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN2,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_KNC1RDS - KNC One TV-Station RDS
    {
        "KNC One TV-Station RDS",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN2,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Composite 2",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_24576Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_CINERGY400 - Terratec Cinergy 400 TV
    {
        "Terratec Cinergy 400 TV",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN4,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x1142153B,
    },
    // SAA7134CARDID_MEDION5044 - Medion 5044
    {
        "Medion 5044",
        0x7134,
        5,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
                0x6000, 0x4000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0x6000, 0x0000,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
                0x6000, 0x0000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0x6000, 0x0000,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
                0x6000, 0x0000,
            },
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_24576Hz,
        0x00006000,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_KWTV713XRF - KWORLD KW-TV713XRF (saa7130)
    // Thanks "b" <b@ki...>
    // this card probably needs GPIO changes but I don't know what they are
   {
        "KWORLD KW-TV713XRF / KUROUTO SHIKOU",
        0x7130,
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_NTSC,
        AUDIOCRYSTAL_NONE,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_MANLIMTV001 - Manli M-TV001 (saa7130)
    // Thanks "Bedo" Bedo@dscaler.forums
    {
        "Manli M-TV001",
        0x7130,
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
                0x6000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
                0x6000, 0x0000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
                0x6000, 0x0000,
            },
        },
        TUNER_LG_B11D_PAL,  // Should be LG TPI8PSB12P PAL B/G
        AUDIOCRYSTAL_NONE,
        0x00006000,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_PRIMETV7133 - PrimeTV 7133 (saa7133)
    // Thanks "Shin'ya Yamaguchi" <yamaguchi@no...>
    {
        "PrimeTV 7133",
        0x7133,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x2000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x4000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x4000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0xE000, 0x8000,
            },
            #endif
        },
        TUNER_PHILIPS_FI1286_NTSC_M_J,  // Should be TCL2002NJ or Philips FI1286 (NTSC M-J)
        AUDIOCRYSTAL_24576Hz,
        0x0018e700,
        NULL,
        StandardSAA7134InputSelect,
        0x01385168,
    },
    // SAA7134CARDID_CINERGY600 - Terratec Cinergy 600 TV
    // Thanks "Michel de Glace" <mglace@my...>
    {
        "Terratec Cinergy 600 TV",
        0x7134,
        5,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN4,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,          
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x1143153b,
    },
    // SAA7134CARDID_MEDION7134 - Medion TV-Tuner 7134 MK2/3
    // Thanks "DavidbowiE" Guest@dscaler.forums
    // Thanks "Josef Schneider" <josef@ne...>
    {
        "Medion TV-Tuner 7134 MK2/3",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x000316be,
    },
    // SAA7134CARDID_TYPHOON90031 - Typhoon TV+Radio (Art.Nr. 90031)
    // Thanks "Tom Zoerner" <tomzo@ne...>
    {
        "Typhoon TV-Radio 90031",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,  // MUTE, card has no audio in
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,  // MUTE, card has no audio in
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_MANLIMTV002 - Manli M-TV002 (saa7130)
    // Thanks "Patrik Gloncak" <gloncak@ho...>
    {
        "Manli M-TV002",
        0x7130,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_LG_B11D_PAL,  // Should be LG TPI8PSB02P PAL B/G
        AUDIOCRYSTAL_NONE,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_VGEAR_MYTV_SAP - V-Gear MyTV SAP PK
    // Thanks "Ken Chung" <kenchunghk2000@ya...>
    {
        "V-Gear MyTV SAP PK",
        0x7134,
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
                0x4400, 0x0400,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0x4400, 0x0400,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
                0x4400, 0x0400,
            },
        },
        TUNER_PHILIPS_PAL_I,
        AUDIOCRYSTAL_32110Hz,
        0x00004400,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_ASUS_TVFM - ASUS TV/FM
    // Thanks "Wolfgang Scholz" <wolfgang.scholz@ka...>
    {
        "ASUS TV/FM",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN4,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x48421043,
    },
    // SAA7134CARDID_AOPEN_VA1000_L2 - Aopen VA1000 Lite2 (saa7130)
    // Thanks "stu" <ausstu@ho...>
    {
        "Aopen VA1000 Lite2",
        0x7130,
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
                0x40, 0x70,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
                0x20, 0x70,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
                0x20, 0x70,
            },
        },
        TUNER_LG_TAPCNEW_PAL,
        AUDIOCRYSTAL_NONE,
        0x00000060,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_ASK_ASVCV300_PCI (saa7130)
    // Thanks "Tetsuya Takahashi" <tetsu_64k@zer...>
    //  - may have Videoport
    //  - may have Transport Stream
    {
        "ASK SELECT AS-VCV300/PCI",
        0x7130,
        2,
        {
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
            },
        },
        TUNER_ABSENT,
        AUDIOCRYSTAL_NONE,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x226e1048,
    },
    // Medion MD-2819 PC-TV-radio card
    // Thanks "Sanel.B" <vlasenica@ya...>
    // Thanks "Mc" <michel.heusinkveld2@wa...>
    // Thanks "Ing. Arno Pucher" <eolruin@ch...>
    {
        "Medion MD-2819 PC-TV-radio card",
        0x7134,
        5,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
                0x00040007, 0x00000006,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000006,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000006,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000005,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0x00040007, 0x00000004,
            },
            #endif
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_32110Hz,
        0x00040007,
        NULL,
        StandardSAA7134InputSelect,
        0xa70b1461,
    },
    // FlyVideo FlyView 3100 (NTSC Version - United States)
    // Thanks "Ryan N. Datsko" <MysticWhiteDragon@ho...>
    // SAA7133 -- not supported
    {
        "FlyVideo FlyView 3100 (no audio)",
        0x7133,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x4000,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x0000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0xE000, 0x8000,
            },
            #endif
        },
        TUNER_PHILIPS_NTSC,
        AUDIOCRYSTAL_NONE,
        0x018e700,
        NULL,
        StandardSAA7134InputSelect,
        0x01385168,
    },
    // Pinnacle PCTV Stereo
    // Thanks "Fabio Maione" <maione@ma...>
    // Thanks "Dr. Uwe Zettl" <uwe.zettl@t...>
    // Thanks "Aristarco" <aristarco@ar...>
    // I2S audio may need to be enabled for this card to work.
    {
        "Pinnacle PCTV Stereo",
        0x7134,
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_MT2050_PAL,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x002b11bd,
    },
    // AverMedia AverTV Studio 305
    // Thanks "Oeoeeia Aieodee" <sid16@ya...>
    {
        "AverMedia AverTV Studio 305",
        0x7130,
        5,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000005,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000006,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000006,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE1,
                0x00040007, 0x00000005,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_DAC,
                0x00040007, 0x00000004,
            },
            #endif
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_NONE,
        0x00040007,
        NULL,
        StandardSAA7134InputSelect,
        0x21151461,
    },
    // Elitegroup EZ-TV
    // Thanks "Arturo Garcia" <argabulk@ho...>
    {
        "Elitegroup EZ-TV",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
        0x4cb41019,
    },
    // ST Lab PCI-TV7130
    // Thanks "Aidan Gill" <schmookoo@ho...>
    {
        "ST Lab PCI-TV7130",
        0x7130,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE2,
                0x7000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0x7000, 0x2000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
                0x7000, 0x2000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_DAC,
                0x7000, 0x3000,
            },
            #endif
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_NONE,
        0x00007000,
        NULL,
        StandardSAA7134InputSelect,
        0x20011131,
    },
    // Lifeview FlyTV Platinum
    // Thanks "Chousw" <chousw@ms...>
    // SAA7133 -- not supported
    {
        "Lifeview FlyTV Platinum (no audio)",
        0x7133,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x4000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
                0xE000, 0x4000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0xE000, 0x8000,
            },
            #endif
        },
        TUNER_PHILIPS_NTSC,
        AUDIOCRYSTAL_NONE,
        0x018e700,
        NULL,
        StandardSAA7134InputSelect,
        0x02145168,
    },
    // Compro VideoMate TV Gold Plus
    // Thanks "Stephen McCormick" <sdmcc@pa...>
    {
        "Compro VideoMate TV Gold Plus",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
                0x1ce780, 0x008080,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE1,
                0x1ce780, 0x008080,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
                0x1ce780, 0x008080,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0x1ce780, 0x0c8000,
            },
            #endif
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_32110Hz,
        0x001ce780,
        NULL,
        StandardSAA7134InputSelect,
        0xc200185b,
    },
/*    // Chronos Video Shuttle II Stereo
    // Thanks "Velizar Velinov" <veli_velinov2001@ya...>
    // Maybe exactly the same as FlyVideo 3000
    {
        "Chronos Video Shuttle II Stereo",
        0x7134,
        6,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_DAC,
                0xE000, 0x0000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x4000,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
                0xE000, 0x2000,
            },
            #if 0
            {
                NULL,
                INPUTTYPE_FINAL,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_NONE,
                0xE000, 0x8000,
            },
            #endif
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        0x0000e000,
        NULL,
        StandardSAA7134InputSelect,
        0x01384e42,
    },*/
    // Much TV Plus IT005
    // Thanks "Norman Jonas" <normanjonas@ar...>
    {
        "Much TV Plus",
        0x7134,
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_DAC,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,          // (Might req mode 6)
                AUDIOINPUTSOURCE_LINE1,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_LG_B11D_PAL,  // Should be LG TPI8PSB02P PAL B/G
        AUDIOCRYSTAL_32110Hz,
        0,
        NULL,
        StandardSAA7134InputSelect,
    },
};

#define SAA7134CARDID_LASTONE (sizeof(m_SAA7134Cards)/sizeof(m_SAA7134Cards[0]))
#define SAA7134CARDID_UNKNOWN 0


static uint AutoDetectCardType( TVCARD * pTvCard )
{
    WORD  DeviceId;
    DWORD SubSystemId;
    uint i;

    if (pTvCard != NULL)
    {
        DeviceId           = pTvCard->params.DeviceId;
        SubSystemId        = pTvCard->params.SubSystemId;

        for (i=0; i < SAA7134CARDID_LASTONE; i++)
        {
            if (m_SAA7134Cards[i].DeviceId == DeviceId &&
                m_SAA7134Cards[i].dwAutoDetectId == SubSystemId &&
                m_SAA7134Cards[i].dwAutoDetectId != 0)
            {
                dprintf1("SAA713x: Autodetect found %s\n", GetCardName(i));
                return i;
            }
        }

        ifdebug2(SubSystemId != 0, "SAA713x: unknown card 0x%04X %08lX", DeviceId, SubSystemId);

        #if 0
        LOG(0, "SAA713x: Autodetect found an unknown card with the following");
        LOG(0, "SAA713x: properties.  Please email the author and quote the");
        LOG(0, "SAA713x: following numbers, as well as which card you have,");
        LOG(0, "SAA713x: so it can be added to the list:");
        LOG(0, "SAA713x: DeviceId: 0x%04x, SubVendorSystemId: 0x%04x%04x,",
            DeviceId, SubSystemVendorId, SubSystemId);
        #endif
    }
    else
        fatal0("SAA7134-AutoDetectCardType: illegal NULL ptr param");

    return SAA7134CARDID_UNKNOWN;
}

static uint AutoDetectTuner( TVCARD * pTvCard, uint CardId )
{
    return m_SAA7134Cards[CardId].TunerId;
}


static bool GetIffType( TVCARD * pTvCard, bool * pIsMono )
{
    if ((pTvCard == NULL) || (pIsMono == NULL))
        fatal0("SAA7134-GetIffType: illegal NULL ptr param");

    return FALSE;
}

static uint GetPllType( TVCARD * pTvCard, uint CardId )
{
    if (pTvCard == NULL)
        fatal0("SAA7134-GetPllType: illegal NULL ptr param");

   return 0;
}

static void GetI2cScanRange( struct TVCARD_struct * pTvCard, uint * pStart, uint * pStop )
{
   if ((pTvCard != NULL) && (pStart != NULL) && (pStop != NULL))
   {
      *pStart = 0xC0;
      *pStop  = 0xCE;
   }
   else
      fatal0("SAA7134-GetI2cScanRange: illegal NULL ptr param");
}

// ---------------------------------------------------------------------------
// Query if the PCI card supports ACPI (power management)
//
static bool SupportsAcpi( TVCARD * pTvCard )
{
   return TRUE;
}

static uint GetNumInputs( TVCARD * pTvCard )
{
    uint  m_CardType;
    uint  count = 0;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if (m_CardType < SAA7134CARDID_LASTONE)
        {
            count = m_SAA7134Cards[m_CardType].NumInputs;
        }
        else
            debug2("SA7134-GetNumInputs: invalid card idx %d (>= %d)", m_CardType, SAA7134CARDID_LASTONE);
    }
    else
        fatal0("SAA7134-GetNumInputs: illegal NULL ptr param");

    return count;
}

static const char * GetInputName( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    const char * pName = NULL;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < SAA7134CARDID_LASTONE) && (nInput < m_SAA7134Cards[m_CardType].NumInputs))
        {
            pName = m_SAA7134Cards[m_CardType].Inputs[nInput].szName;
        }
        else
            debug4("SAA7134-GetInputName: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, SAA7134CARDID_LASTONE, nInput, m_SAA7134Cards[m_CardType].NumInputs);
    }
    else
        fatal0("SAA7134-GetInputName: illegal NULL ptr param");

    return pName;
}


static bool IsInputATuner( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    bool  result = FALSE;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < SAA7134CARDID_LASTONE) && (nInput < m_SAA7134Cards[m_CardType].NumInputs))
        {
            result = (m_SAA7134Cards[m_CardType].Inputs[nInput].InputType == INPUTTYPE_TUNER);
        }
        else
            debug4("SA7134-IsInputATuner: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, SAA7134CARDID_LASTONE, nInput, m_SAA7134Cards[m_CardType].NumInputs);
    }
    else
        fatal0("SAA7134-IsInputATuner: illegal NULL ptr param");

    return result;
}


static bool IsInputSVideo( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    bool  result = FALSE;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < SAA7134CARDID_LASTONE) && (nInput < m_SAA7134Cards[m_CardType].NumInputs))
        {
            result = (m_SAA7134Cards[m_CardType].Inputs[nInput].InputType == INPUTTYPE_SVIDEO);
        }
        else
            debug4("SA7134-IsInputSVideo: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, SAA7134CARDID_LASTONE, nInput, m_SAA7134Cards[m_CardType].NumInputs);
    }
    else
        fatal0("SAA7134-IsInputSVideo: illegal NULL ptr param");

    return result;
}


static const char * GetCardName( TVCARD * pTvCard, uint CardId )
{
    const char * pName = NULL;

    if (pTvCard != NULL)
    {
        if (CardId < SAA7134CARDID_LASTONE)
            pName = m_SAA7134Cards[CardId].szName;
        else
            debug2("SAA7134-GetCardName: invalid card idx %d (>= %d)", CardId, SAA7134CARDID_LASTONE);
    }
    else
        fatal0("SAA7134-GetCardName: illegal NULL ptr param");

    return pName;
}


static bool SetVideoSource( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    bool  result = FALSE;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < SAA7134CARDID_LASTONE) && (nInput < m_SAA7134Cards[m_CardType].NumInputs))
        {
            m_SAA7134Cards[m_CardType].pInputSwitchFunction(pTvCard, nInput);
            result = TRUE;
        }
        else
            debug4("SA7134-SetVideoSource: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, SAA7134CARDID_LASTONE, nInput, m_SAA7134Cards[m_CardType].NumInputs);
    }
    else
        fatal0("SAA7134-SetVideoSource: illegal NULL ptr param");

    return result;
}


static void StandardSAA7134InputSelect(TVCARD * pTvCard, int nInput)
{
    eVideoInputSource VideoInput;
    uint  m_CardType = pTvCard->params.cardId;
    BYTE Mode;

    // -1 for finishing clean up
    if(nInput == -1)
    {
        // do nothing
        return;
    }

    if(nInput >= m_SAA7134Cards[m_CardType].NumInputs)
    {
        debug1("Input Select Called for invalid input %d", nInput);
        nInput = m_SAA7134Cards[m_CardType].NumInputs - 1;
    }
    if(nInput < 0)
    {
        debug1("Input Select Called for invalid input %d", nInput);
        nInput = 0;
    }

    VideoInput = m_SAA7134Cards[m_CardType].Inputs[nInput].VideoInputPin;

    /// There is a 1:1 correlation between (int)eVideoInputSource
    /// and SAA7134_ANALOG_IN_CTRL1_MODE
    Mode = (VideoInput == VIDEOINPUTSOURCE_NONE) ? 0x00 : VideoInput;

    switch (m_SAA7134Cards[m_CardType].Inputs[nInput].InputType)
    {
        case INPUTTYPE_SVIDEO:
            OrDataByte(SAA7134_LUMA_CTRL, SAA7134_LUMA_CTRL_BYPS);

            switch (VideoInput)
            {
            // This new mode sets Y-channel with automatic
            // gain control and gain control for C-channel
            // linked to Y-channel
            case VIDEOINPUTSOURCE_PIN0: Mode = 0x08; break;
            case VIDEOINPUTSOURCE_PIN1: Mode = 0x09; break;
            default:
                // NEVER_GET_HERE;
                break;
            }
            break;
        case INPUTTYPE_TUNER:
        case INPUTTYPE_COMPOSITE:
        case INPUTTYPE_CCIR:
        default:
            AndDataByte(SAA7134_LUMA_CTRL, (BYTE)~SAA7134_LUMA_CTRL_BYPS);
            break;
    }

    MaskDataByte(SAA7134_ANALOG_IN_CTRL1, Mode, 0x0F);

    // GPIO settings
    if (m_SAA7134Cards[m_CardType].dwGPIOMode != 0)
    {
        MaskDataDword(SAA7134_GPIO_GPMODE, m_SAA7134Cards[m_CardType].dwGPIOMode, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS,
                      m_SAA7134Cards[m_CardType].Inputs[nInput].dwGPIOStatusBits,
                      m_SAA7134Cards[m_CardType].Inputs[nInput].dwGPIOStatusMask);
    }
}

// ---------------------------------------------------------------------------
// Fill interface struct
//
static const TVCARD_CFG SAA7134Typ_Interface =
{
   GetCardName,
   AutoDetectCardType,
   AutoDetectTuner,
   GetIffType,
   GetPllType,
   GetI2cScanRange,
   SupportsAcpi,
   GetNumInputs,
   GetInputName,
   IsInputATuner,
   IsInputSVideo,
   SetVideoSource,
};

void SAA7134Typ_GetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      pTvCard->cfg = &SAA7134Typ_Interface;
   }
   else
      fatal0("SAA7134Typ-GetInterface: NULL ptr param");
}

