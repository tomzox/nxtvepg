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
 *  DScaler #Id: SAA7134Card_Types.cpp,v 1.33 2003/07/31 05:01:38 atnak Exp #
 *
 *  $Id: saa7134_typ.c,v 1.13 2003/09/02 19:57:19 tom Exp tom $
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
} TInputType;
     
/// Defines the specific settings for a given card
#define INPUTS_PER_CARD 7
typedef struct
{
    LPCSTR szName;
    int NumInputs;
    TInputType Inputs[INPUTS_PER_CARD];
    eTunerId TunerId;
    /// The type of clock crystal the card has
    eAudioCrystal AudioCrystal;
    /// Any card specific initialization - may be NULL
    void (*pInitCardFunction)(void);
    /** Function used to switch between sources
        Cannot be NULL
        Default is StandardBT848InputSelect
    */
    void (*pInputSwitchFunction)(TVCARD * pTvCard, int nInput);
} TCardType;

static void FLYVIDEO3000CardInputSelect(TVCARD * pTvCard, int nInput);
static void FLYVIDEO2000CardInputSelect(TVCARD * pTvCard, int nInput);
static void MEDION5044CardInputSelect(TVCARD * pTvCard, int nInput);
static void KWTV713XRFCardInputSelect(TVCARD * pTvCard, int nInput);
static void PrimeTV7133CardInputSelect(TVCARD * pTvCard, int nInput);
static void ManliMTV001CardInputSelect(TVCARD * pTvCard, int nInput);
static void ManliMTV002CardInputSelect(TVCARD * pTvCard, int nInput);
static void VGearMyTVSAPCardInputSelect(TVCARD * pTvCard, int nInput);
static void AOpenVA1000L2CardInputSelect(TVCARD * pTvCard, int nInput);
static void StandardSAA7134InputSelect(TVCARD * pTvCard, int nInput);

/// SAA713x Card Ids
typedef enum
{
    SAA7134CARDID_UNKNOWN = 0,
    SAA7134CARDID_PROTEUSPRO,
    SAA7134CARDID_FLYVIDEO3000,
    SAA7134CARDID_FLYVIDEO2000,
    SAA7134CARDID_EMPRESS,
    SAA7134CARDID_MONSTERTV,
    SAA7134CARDID_TEVIONMD9717,
    SAA7134CARDID_KNC1RDS,
    SAA7134CARDID_CINERGY400,
    SAA7134CARDID_MEDION5044,
    SAA7134CARDID_KWTV713XRF,
    SAA7134CARDID_MANLIMTV001,
    SAA7134CARDID_PRIMETV7133,
    SAA7134CARDID_CINERGY600,
    SAA7134CARDID_MEDION7134,
    SAA7134CARDID_TYPHOON90031,
    SAA7134CARDID_MANLIMTV002,
    SAA7134CARDID_VGEAR_MYTV_SAP,
    SAA7134CARDID_ASUS_TVFM,
    SAA7134CARDID_AOPEN_VA1000_L2,
    SAA7134CARDID_ASK_ASVCV300_PCI,
    SAA7134CARDID_LASTONE,
} eSAA7134CardId;

/// used to store the ID for autodetection
typedef struct
{
    WORD DeviceId;
    WORD SubSystemVendorId;
    WORD SubSystemId;
    eSAA7134CardId CardId;
} TAutoDetectSAA7134;

static const TCardType m_SAA7134Cards[] =
{
    // SAA7134CARDID_UNKNOWN - Unknown Card
    {
        "*Unknown Card*",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_PROTEUSPRO - Proteus Pro [philips reference design]
    {
        "Proteus Pro [philips reference design]",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_FLYVIDEO3000 - LifeView FlyVIDEO3000
    {
        "LifeView FlyVIDEO3000",
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
                VIDEOINPUTSOURCE_PIN3,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
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
        NULL,
        FLYVIDEO3000CardInputSelect,
    },
    // SAA7134CARDID_FLYVIDEO2000 - LifeView FlyVIDEO2000 (saa7130)
    {
        "LifeView FlyVIDEO2000",
        5,
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
            {
                "Composite over S-Video",
                INPUTTYPE_COMPOSITE,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_LG_TAPCNEW_PAL,
        AUDIOCRYSTAL_NONE,
        NULL,
        FLYVIDEO2000CardInputSelect,
    },
    // SAA7134CARDID_EMPRESS - EMPRESS (has TS, i2srate=48000, has CCIR656 video out)
    {
        "EMPRESS",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_MONSTERTV - SKNet Monster TV
    {
        "SKNet Monster TV",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_TEVIONMD9717 - Tevion MD 9717
    {
        "Tevion MD 9717",
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
                "Composite over S-Video",
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
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_24576Hz,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_KNC1RDS - KNC One TV-Station RDS
    {
        "KNC One TV-Station RDS",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_CINERGY400 - Terratec Cinergy 400 TV
    {
        "Terratec Cinergy 400 TV",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_MEDION5044 - Medion 5044
    {
        "Medion 5044",
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
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
            {
                "Composite over S-Video",
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
            {
                "Radio",
                INPUTTYPE_RADIO,
                VIDEOINPUTSOURCE_NONE,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_FM1216ME_MK3,
        AUDIOCRYSTAL_24576Hz,
        NULL,
        MEDION5044CardInputSelect,
    },
    // SAA7134CARDID_KWTV713XRF - KWORLD KW-TV713XRF (saa7130)
    // Thanks "b" <b@ki...>
    {
        "KWORLD KW-TV713XRF / KUROUTO SHIKOU",
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
        NULL,
        KWTV713XRFCardInputSelect,
    },
    // SAA7134CARDID_MANLIMTV001 - Manli M-TV001 (saa7130)
    // Thanks "Bedo" Bedo@dscaler.forums
    {
        "Manli M-TV001",
        3,
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
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
            },
        },
        TUNER_LG_B11D_PAL,  // Should be LG TPI8PSB12P PAL B/G
        AUDIOCRYSTAL_NONE,
        NULL,
        ManliMTV001CardInputSelect,
    },
    // SAA7134CARDID_PRIMETV7133 - PrimeTV 7133 (saa7133)
    // Thanks "Shin'ya Yamaguchi" <yamaguchi@no...>
    {
        "PrimeTV 7133",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN1,
                AUDIOINPUTSOURCE_LINE1,
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
        },
        TUNER_PHILIPS_FI1286_NTSC_M_J,  // Should be TCL2002NJ or Philips FI1286 (NTSC M-J)
        AUDIOCRYSTAL_24576Hz,
        NULL,
        PrimeTV7133CardInputSelect,
    },
    // SAA7134CARDID_CINERGY600 - Terratec Cinergy 600 TV
    // Thanks "Michel de Glace" <mglace@my...>
    {
        "Terratec Cinergy 600 TV",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_MEDION7134 - Medion TV-Tuner 7134 MK2/3
    // Thanks "DavidbowiE" Guest@dscaler.forums
    // Thanks "Josef Schneider" <josef@ne...>
    {
        "Medion TV-Tuner 7134 MK2/3",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_TYPHOON90031 - Typhoon TV+Radio (Art.Nr. 90031)
    // Thanks "Tom Zoerner" <tomzo@ne...>
    {
        "Typhoon TV+Radio 90031",
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
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_MANLIMTV002 - Manli M-TV002 (saa7130)
    // Thanks "Patrik Gloncak" <gloncak@ho...>
    {
        "Manli M-TV002",
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
        NULL,
        ManliMTV002CardInputSelect,
    },
    // SAA7134CARDID_VGEAR_MYTV_SAP - V-Gear MyTV SAP PK
    // Thanks "Ken Chung" <kenchunghk2000@ya...>
    {
        "V-Gear MyTV SAP PK",
        3,
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
        },
        TUNER_PHILIPS_PAL_I,
        AUDIOCRYSTAL_32110Hz,
        NULL,
        VGearMyTVSAPCardInputSelect,
    },
    // SAA7134CARDID_ASUS_TVFM - ASUS TV/FM
    // Thanks "Wolfgang Scholz" <wolfgang.scholz@ka...>
    {
        "ASUS TV/FM",
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
                INPUTTYPE_SVIDEO,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE2,
            },
        },
        TUNER_PHILIPS_PAL,
        AUDIOCRYSTAL_32110Hz,
        NULL,
        StandardSAA7134InputSelect,
    },
    // SAA7134CARDID_AOPEN_VA1000_L2 - Aopen VA1000 Lite2 (saa7130)
    // Thanks "stu" <ausstu@ho...>
    {
        "Aopen VA1000 Lite2",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                VIDEOINPUTSOURCE_PIN0,
                AUDIOINPUTSOURCE_LINE1,
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
        },
        TUNER_LG_TAPCNEW_PAL,
        AUDIOCRYSTAL_NONE,
        NULL,
        AOpenVA1000L2CardInputSelect,
    },
    // SAA7134CARDID_ASK_ASVCV300_PCI (saa7130)
    // Thanks "Tetsuya Takahashi" <tetsu_64k@zer...>
    //  - may have Videoport
    //  - may have Transport Stream
    {
        "ASK SELECT AS-VCV300/PCI",
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
        NULL,
        StandardSAA7134InputSelect,
    },
};


static const TAutoDetectSAA7134 m_AutoDetectSAA7134[] =
{
    // How to use RegSpy dump header information:
    //
    // Vendor ID:           0x1131  (drop this value)
    // Device ID:           0xDDDD
    // Subsystem ID:        0xSSSSVVVV
    //
    // { 0xDDDD, 0xVVVV, 0xSSSS, SAA7134CARDID_    },

    // DeviceId, Subsystem vendor Id, Subsystem Id, Card Id
    { 0x7134, 0x1131, 0x0000, SAA7134CARDID_UNKNOWN             },
    { 0x7130, 0x1131, 0x0000, SAA7134CARDID_UNKNOWN             },
    { 0x7134, 0x1131, 0x2001, SAA7134CARDID_PROTEUSPRO          },
    { 0x7134, 0x1131, 0x6752, SAA7134CARDID_EMPRESS             },
    { 0x7134, 0x1131, 0x4E85, SAA7134CARDID_MONSTERTV           },
    { 0x7134, 0x153B, 0x1142, SAA7134CARDID_CINERGY400          },
    { 0x7130, 0x5168, 0x0138, SAA7134CARDID_FLYVIDEO2000        },
    { 0x7133, 0x5168, 0x0138, SAA7134CARDID_PRIMETV7133         },
    { 0x7134, 0x153b, 0x1143, SAA7134CARDID_CINERGY600          },
    { 0x7134, 0x16be, 0x0003, SAA7134CARDID_MEDION7134          },
    { 0x7134, 0x1043, 0x4842, SAA7134CARDID_ASUS_TVFM           },
    { 0x7130, 0x1048, 0x226e, SAA7134CARDID_ASK_ASVCV300_PCI    },
};


static uint AutoDetectCardType( TVCARD * pTvCard )
{
    WORD DeviceId;
    WORD SubSystemId;
    WORD SubSystemVendorId;
    int ListSize;
    int i;

    if (pTvCard != NULL)
    {
        DeviceId           = pTvCard->params.DeviceId;
        SubSystemId        = (pTvCard->params.SubSystemId >> 16);
        SubSystemVendorId  = (pTvCard->params.SubSystemId & 0x0000FFFF);

        ListSize = sizeof(m_AutoDetectSAA7134)/sizeof(TAutoDetectSAA7134);

        for (i=0; i < ListSize; i++)
        {
            if (m_AutoDetectSAA7134[i].DeviceId == DeviceId &&
                m_AutoDetectSAA7134[i].SubSystemId == SubSystemId &&
                m_AutoDetectSAA7134[i].SubSystemVendorId == SubSystemVendorId)
            {
                dprintf1("SAA713x: Autodetect found %s\n", GetCardName(m_AutoDetectSAA7134[i].CardId));
                return m_AutoDetectSAA7134[i].CardId;
            }
        }

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



/*
 *  LifeView's audio chip connected accross GPIO mask 0xE000.
 *  Used by FlyVideo3000, FlyVideo2000 and PrimeTV 7133.
 *  (Below information is an unverified guess --AtNak)
 *
 *  NNNx
 *  ^^^
 *  |||- 0 = Normal, 1 = BTSC processing on ?
 *  ||-- 0 = Internal audio, 1 = External line pass through
 *  |--- 0 = Audio processor ON, 1 = Audio processor OFF
 *
 *  Use Normal/Internal/Audio Processor ON for FM Radio
 */


static void FLYVIDEO3000CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0: // Tuner
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x8000, 0xE000);
        break;
    case 1: // Composite
    case 2: // S-Video
    case 3: // Composite over S-Video
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x4000, 0xE000);
        break;
    case 4: // Radio
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x0000, 0xE000);
        break;
    case -1: // Ending cleanup
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x8000, 0xE000);
        break;
    default:
        break;
    }
}


static void FLYVIDEO2000CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0: // Tuner
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x0000, 0xE000);
        break;
    case 1: // Composite
    case 2: // S-Video
    case 3: // Composite over S-Video
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x4000, 0xE000);
        break;
    case 4: // Radio
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x2000, 0xE000);
        break;
    case -1: // Ending cleanup
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x8000, 0xE000);
        break;
    default:
        break;
    }
}


static void MEDION5044CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0:
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x6000, 0x6000);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x4000, 0x6000);
        break;
    case 1:
    case 2:
    case 3:
    case 4:
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x6000, 0x6000);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x0000, 0x6000);
        break;
    default:
        break;
    }
}


static void KWTV713XRFCardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);

    // this card probably needs GPIO changes but I don't
    // know what they are
}


static void PrimeTV7133CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0: // Tuner
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x2000, 0xE000);
        break;
    case 1: // Composite
    case 2: // S-Video
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x4000, 0xE000);
        break;
    case -1: // Ending cleanup
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x0018e700, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x8000, 0xE000);
        break;
    default:
        break;
    }
}


static void ManliMTV001CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0:
    case 1:
    case 2:
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x6000, 0x6000);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x0000, 0x6000);
        break;
    default:
        break;
    }
}


static void ManliMTV002CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    /*
    switch(nInput)
    {
    case 0: // Tuner
    case 1: // Composite
    case 2: // S-Video
    case -1: // Ending cleanup
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x8000, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x8000, 0x8000);
        break;
    case 3: // Radio
        // Unverified GPIO setup
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x8000, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x0000, 0x8000);
        break;
    default:
        break;
    }
    */
}


static void VGearMyTVSAPCardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0: // Tuner
    case 1: // Composite
    case 2: // S-Video
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x4400, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x0400, 0x4400);
        break;
    default:
        break;
    }
}


static void AOpenVA1000L2CardInputSelect(TVCARD * pTvCard, int nInput)
{
    StandardSAA7134InputSelect(pTvCard, nInput);
    switch(nInput)
    {
    case 0: // Tuner
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x40, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x70, 0x40);
        break;
    case 1: // Composite
    case 2: // S-Video
        MaskDataDword(SAA7134_GPIO_GPMODE, 0x20, 0x0EFFFFFF);
        MaskDataDword(SAA7134_GPIO_GPSTATUS, 0x70, 0x20);
        break;
    default:
        break;
    }
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

