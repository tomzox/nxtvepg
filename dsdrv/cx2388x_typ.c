/*
 *  Conexant CX2388x card types list
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
 *    This module contains a list of TV cards based on the Conexant CX2388x
 *    chip family.  If offers functions to autodetect cards (based on their
 *    PCI subsystem ID which can be stored in the EEPROM on the card) and
 *    tuners (for certain vendors only, if type is stored in EEPROM),
 *    enumerate card and input lists and card-specific functions to switch
 *    the video input channel.
 *
 *
 *  Authors:
 *    Copyright (c) 2002 John Adcock.  All rights reserved.
 *
 *  DScaler #Id: CX2388xCard_Types.cpp,v 1.22 2004/03/10 17:44:03 to_see Exp #
 *
 *  $Id: cx2388x_typ.c,v 1.14 2004/12/26 21:47:20 tom Exp tom $
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
#include "dsdrv/cx2388x_reg.h"
#include "dsdrv/cx2388x_typ.h"


/// Different types of input currently supported
typedef enum
{
    /// standard composite input
    INPUTTYPE_TUNER,
    /// standard composite input
    INPUTTYPE_COMPOSITE,
    /// standard s-video input
    INPUTTYPE_SVIDEO,
    /// Digital CCIR656 input on the GPIO pins
    INPUTTYPE_CCIR,
    /// Shows Colour Bars for testing
    INPUTTYPE_COLOURBARS,
} eInputType;

static void StandardInputSelect( TVCARD * pTvCard, uint nInput);
static void MSIPalInputSelect( TVCARD * pTvCard, uint nInput);
static void PlayHDInputSelect( TVCARD * pTvCard, uint nInput);
static void AsusInputSelect( TVCARD * pTvCard, uint nInput);
static void LeadtekInputSelect( TVCARD * pTvCard, uint nInput );
static void AverTV303InputSelect( TVCARD * pTvCard, uint nInput );
static void StandardSetFormat( TVCARD * pTvCard, uint nInput, eVideoFormat Format, BOOL IsProgressive);

/// Defines each input on a card
typedef struct
{
    /// Name of the input
    LPCSTR szName;
    /// Type of the input
    eInputType InputType;
    /// Which mux on the card is to be used
    BYTE MuxSelect;
    /// Audio input mux
    DWORD GPIOFlags;
} TInputType;

/// Defines the specific settings for a given card
#define CT_INPUTS_PER_CARD 9
typedef struct
{
    LPCSTR szName;
    int NumInputs;
    TInputType Inputs[CT_INPUTS_PER_CARD];

    /// Any card specific initialization - may be NULL
    void (*pInitCardFunction)(void);
    /// Any card specific routine required to stop capture - may be NULL
    //void (CCX2388xCard::*pStopCaptureCardFunction)(void);
    /** Function used to switch between sources
        Cannot be NULL
        Default is StandardBT848InputSelect
    */
    void (*pInputSwitchFunction)(TVCARD * pTvCard, uint nInput);
    /// Function to set Contrast and Brightness Default SetAnalogContrastBrightness
    //void (*pSetContrastBrightness)(BYTE, BYTE);
    /// Function to set Hue Default SetAnalogHue
    //void (*pSetHue)(BYTE);
    /// Function to set SaturationU Default SetAnalogSaturationU
    //void (*pSetSaturationU)(BYTE);
    /// Function to set SaturationV Default SetAnalogSaturationV
    //void (*pSetSaturationV)(BYTE);
    /// Function to set Format Default SetFormat
    void (*pSetFormat)(TVCARD *, uint, eVideoFormat, BOOL);
    eTunerId TunerId;
    //int MenuId;
} TCardType;

typedef enum
{
    CX2388xCARD_UNKNOWN = 0,
    CX2388xCARD_CONEXANT_EVK,
    CX2388xCARD_CONEXANT_EVK_PAL,
    CX2388xCARD_HOLO3D,
    CX2388xCARD_PIXELVIEW_XCAPTURE,
    CX2388xCARD_MSI_TV_ANYWHERE_NTSC,
    CX2388xCARD_MSI_TV_ANYWHERE_PAL,
    CX2388xCARD_ASUS,
    CX2388xCARD_PLAYHD,
    CX2388xCARD_HAUPPAUGE_PCI_FM,
    CX2388xCARD_PIXELVIEW_XCAPTURE_PDIMOD,
    CX2388xCARD_LEADTEK_WINFAST_EXPERT,
    CX2388xCARD_MSI_TV_ANYWHERE_MASTER_PAL,
    CX2388xCARD_ATI_WONDER_PRO,
    CX2388xCARD_HAUPPAUGE_PCI_FM_TUNERSOUND,
    CX2388xCARD_PIXELVIEW_PLAYTV_ULTRA_TUNERSOUND,
    CX2388xCARD_KWORLD_TV_STEREO,
    CX2388xCARD_PIXELVIEW_PLAYTV_ULTRA,
    CX2388xCARD_AVERTV_303,
    CX2388xCARD_LASTONE,
} eCX2388xCardId;

/// used to store the ID for autodection
typedef struct
{
    DWORD ID;
    eCX2388xCardId CardId;
    char* szName;
} TAutoDectect;

static const TCardType m_TVCards[CX2388xCARD_LASTONE] =
{
    // Card Number 0 - Unknown
    {
        "*Unknown*",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Colour Bars",
                INPUTTYPE_COLOURBARS,
                0,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_NTSC,
        //IDC_CX2388X,
    },
    {
        "Conexant CX23880 TV/FM EVK",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Colour Bars",
                INPUTTYPE_COLOURBARS,
                0,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_NTSC,
        //IDC_CX2388X,
    },
    {
        "Conexant CX23880 TV/FM EVK (PAL)",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Colour Bars",
                INPUTTYPE_COLOURBARS,
                0,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_PAL,
        //IDC_CX2388X,
    },
    {
        "Holo 3d Graph",
        9,
        {
            {
                "Component",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "RGsB",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "SDI",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "Composite G",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "Composite B",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "Composite R",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "Composite BNC",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
            {
                "PDI",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
        },
        NULL,  //InitH3D,
        //NULL,
        StandardInputSelect,  //H3DInputSelect,
        //SetH3DContrastBrightness,
        //SetH3DHue,
        //SetH3DSaturationU,
        //SetH3DSaturationV,
        StandardSetFormat, //H3DSetFormat,
        TUNER_ABSENT,
        //IDC_CX2388X_H3D,
    },
    {
        "PixelView XCapture",
        2,
        {
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_ABSENT,
        //IDC_CX2388X,
    },
    {
        "MSI TV@nywhere (NTSC)",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        MSIPalInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_MT2032,
        //IDC_CX2388X,
    },
    {
        "MSI TV@nywhere (PAL)",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        MSIPalInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_MT2032_PAL,
        //IDC_CX2388X,
    },
    {
        "Asus TV Tuner 880 NTSC",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        AsusInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_USER_SETUP,
        //IDC_CX2388X,
    },
    {
        "Prolink PlayTV HD",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x00000000,
            },
        },
        NULL,
        //PlayHDStopCapture,
        PlayHDInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_USER_SETUP,
        //IDC_CX2388X,
    },
    // Card info from Tom Zoerner
    {
        "Hauppauge WinTV 34xxx models",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x0000ff00,

            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x0000ff02,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x0000ff02,
            },
            {   // card has no composite in, but comes with a Cinch to S-Video adapter
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x0000ff02,
            },
            // FM radio input omitted
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        // \todo add eeprom read functionality
        // these cards seem similar to the bt848 except that
        // the contents are shifted by 8 bytes
		// ...fixed 21.02.2004 to_see
        TUNER_AUTODETECT,
        //IDC_CX2388X,
    },
    {
        "PixelView XCapture With PDI Mod",
        3,
        {
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "PDI",
                INPUTTYPE_CCIR,
                3,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_ABSENT,
        //IDC_CX2388X,
    },
    {
        "Leadtek WinFast TV2000 XP Expert",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        LeadtekInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_FM1216ME_MK3,
        //IDC_CX2388X,
    },
    {
        "MSI TV@nywhere Master",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            {
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x00000000,
            },
        },
        NULL,
        //NULL,
        MSIPalInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_MT2050_PAL,
        //IDC_CX2388X,
    },
    // GPIO's from Allen
    {
        "ATI TV Wonder Pro",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x000003ff,
            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x000003fe,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x000003fe,
            },
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_4IN1,
        //IDC_CX2388X,
    },
    // Card info from Tom Zoerner
    {
        "Hauppauge WinTV 34xxx models (Mono Tuner Sound)",
        4,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x0000ff01,

            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x0000ff02,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x0000ff02,
            },
            {   // card has no composite in, but comes with a Cinch to S-Video adapter
                "Composite Over S-Video",
                INPUTTYPE_COMPOSITE,
                2,
                0x0000ff02,
            },
            // FM radio input omitted
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        // \todo add eeprom read functionality
        // these cards seem similar to the bt848 except that
        // the contents are shifted by 8 bytes
        // ...fixed 21.02.2004 to_see
        TUNER_AUTODETECT,
        //IDC_CX2388X,
    },
    
    // Card info from Denis Love
    {
        "PixelView PlayTV Ultra (Mono Tuner Sound)",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x0000ff00,

            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000ff1,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000ff1,
            },
            // FM radio input omitted
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        // \todo support for onboard TDA9874
        // this is only mono sound from tuner
        TUNER_PHILIPS_PAL,
        //IDC_CX2388X,
    },
    // Card Info from trfillos@...
    // this card has no eeprom.
    {
        "K-World DV/AV Expert TV Stereo",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x000007f8,

            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x000004ff,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x000004ff,
            },
            // FM radio input omitted
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_FM1216ME_MK3,
        //IDC_CX2388X,
    },
    // Card info from tarambuka3500
    {
        "PixelView PlayTV Ultra",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x0000bff1,

            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x0000bff3,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x0000bff3,
            },
            // FM radio input omitted
        },
        NULL,
        //NULL,
        StandardInputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_PAL,
        //IDC_CX2388X,
    },
    
    // Card info Zbigniew Pluta
    {
        "AverTV Studio 303 (M126)",
        3,
        {
            {
                "Tuner",
                INPUTTYPE_TUNER,
                0,
                0x00000000,

            },
            {
                "Composite",
                INPUTTYPE_COMPOSITE,
                1,
                0x00000000,
            },
            {
                "S-Video",
                INPUTTYPE_SVIDEO,
                2,
                0x00000000,
            },
            // FM radio input omitted
        },
        NULL,
        //NULL,
        AverTV303InputSelect,
        //SetAnalogContrastBrightness,
        //SetAnalogHue,
        //SetAnalogSaturationU,
        //SetAnalogSaturationV,
        StandardSetFormat,
        TUNER_PHILIPS_FM1216ME_MK3,
        //IDC_CX2388X,
    },
};

static const TAutoDectect m_AutoDectect[] =
{
    { 0x006614F1, CX2388xCARD_CONEXANT_EVK, "Conexant CX23880 TV/FM EVK" },
    { 0x00f81002, CX2388xCARD_ATI_WONDER_PRO, "ATI Wonder Pro"},
    //Tee Added support for PAL EVK and also added support for SSVID
    { 0x016614F1, CX2388xCARD_CONEXANT_EVK_PAL, "Conexant CX23880 PAL TV/FM EVK" },
    { 0x48201043, CX2388xCARD_ASUS, "Asus 880" },
    { 0x34010070, CX2388xCARD_HAUPPAUGE_PCI_FM, "Hauppauge" },
    { 0x34000070, CX2388xCARD_HAUPPAUGE_PCI_FM, "Hauppauge" },
    { 0x6611107D, CX2388xCARD_LEADTEK_WINFAST_EXPERT, "Leadtek WinFast TV2000 XP Expert" }, // PAL
    { 0x6613107D, CX2388xCARD_LEADTEK_WINFAST_EXPERT, "Leadtek WinFast TV2000 XP Expert" }, // NTSC
    { 0x86061462, CX2388xCARD_MSI_TV_ANYWHERE_MASTER_PAL, "MSI TV@nywhere Master"},
    { 0x48111554, CX2388xCARD_PIXELVIEW_PLAYTV_ULTRA, "PixelView PlayTV Ultra" },
    { 0x088317DE, CX2388xCARD_KWORLD_TV_STEREO, "K-World DV/AV Expert TV Stereo" }, // NTSC
    { 0x088217DE, CX2388xCARD_KWORLD_TV_STEREO, "K-World DV/AV Expert TV Stereo" }, // PAL
    { 0x000b1461, CX2388xCARD_AVERTV_303, "AverTV Studio 303 (M126)" },
    { 0, (eCX2388xCardId)-1, NULL }
};

static const eTunerId m_Tuners_Hauppauge_CX2388x_Card[]=
{
    TUNER_ABSENT,
    TUNER_ABSENT,                       //"External"
    TUNER_ABSENT,                       //"Unspecified"
    TUNER_PHILIPS_PAL,                  //"Philips FI1216"
    //4
    TUNER_PHILIPS_SECAM,                //"Philips FI1216MF"
    TUNER_PHILIPS_NTSC,                 //"Philips FI1236"
    TUNER_PHILIPS_PAL_I,                //"Philips FI1246"
    TUNER_PHILIPS_PAL_DK,               //"Philips FI1256"
    //8
    TUNER_PHILIPS_PAL,                  //"Philips FI1216 MK2"
    TUNER_PHILIPS_SECAM,                //"Philips FI1216MF MK2"
    TUNER_PHILIPS_NTSC,                 //"Philips FI1236 MK2"
    TUNER_PHILIPS_PAL_I,                //"Philips FI1246 MK2"
    //12
    TUNER_PHILIPS_PAL_DK,               //"Philips FI1256 MK2"
    TUNER_TEMIC_4032FY5_NTSC,           //"Temic 4032FY5"
    TUNER_TEMIC_4002FH5_PAL,            //"Temic 4002FH5"
    TUNER_TEMIC_4062FY5_PAL_I,          //"Temic 4062FY5"
    //16
    TUNER_PHILIPS_PAL,                  //"Philips FR1216 MK2"
    TUNER_PHILIPS_SECAM,                //"Philips FR1216MF MK2"
    TUNER_PHILIPS_NTSC,                 //"Philips FR1236 MK2"
    TUNER_PHILIPS_PAL_I,                //"Philips FR1246 MK2"
    //20
    TUNER_PHILIPS_PAL_DK,               //"Philips FR1256 MK2"
    TUNER_PHILIPS_PAL,                  //"Philips FM1216"
    TUNER_PHILIPS_SECAM,                //"Philips FM1216MF"
    TUNER_PHILIPS_NTSC,                 //"Philips FM1236"
    //24
    TUNER_PHILIPS_PAL_I,                //"Philips FM1246"
    TUNER_PHILIPS_PAL_DK,               //"Philips FM1256"
    TUNER_TEMIC_4036FY5_NTSC,           //"Temic 4036FY5"
    TUNER_ABSENT,                       //"Samsung TCPN9082D"
    //28
    TUNER_ABSENT,                       //"Samsung TCPM9092P"
    TUNER_TEMIC_4006FH5_PAL,            //"Temic 4006FH5"
    TUNER_ABSENT,                       //"Samsung TCPN9085D"
    TUNER_ABSENT,                       //"Samsung TCPB9085P"
    //32
    TUNER_ABSENT,                       //"Samsung TCPL9091P"
    TUNER_TEMIC_4039FR5_NTSC,           //"Temic 4039FR5"
    TUNER_PHILIPS_MULTI,                //"Philips FQ1216 ME"
    TUNER_TEMIC_4066FY5_PAL_I,          //"Temic 4066FY5"
    //36
    TUNER_PHILIPS_NTSC,                 //"Philips TD1536"
    TUNER_PHILIPS_NTSC,                 //"Philips TD1536D"
    TUNER_PHILIPS_NTSC,                 //"Philips FMR1236"
    TUNER_ABSENT,                       //"Philips FI1256MP"
    //40
    TUNER_ABSENT,                       //"Samsung TCPQ9091P"
    TUNER_TEMIC_4006FN5_PAL,            //"Temic 4006FN5"
    TUNER_TEMIC_4009FR5_PAL,            //"Temic 4009FR5"
    TUNER_TEMIC_4046FM5_MULTI,          //"Temic 4046FM5"
    //44
    TUNER_TEMIC_4009FN5_PAL,            //"Temic 4009FN5"
    TUNER_ABSENT,                       //"Philips TD1536D_FH_44"
    TUNER_LG_R01F_NTSC,                 //"LG TPI8NSR01F"
    TUNER_LG_B01D_PAL,                  //"LG TPI8PSB01D"
    //48
    TUNER_LG_B11D_PAL,                  //"LG TPI8PSB11D"
    TUNER_LG_I001D_PAL_I,               //"LG TAPC-I001D"
    TUNER_LG_I701D_PAL_I,               //"LG TAPC-I701D"
};


static const char * GetCardName( TVCARD * pTvCard, uint CardId )
{
    const char * pName = NULL;

    if (pTvCard != NULL)
    {
        if (CardId < CX2388xCARD_LASTONE)
            pName = m_TVCards[CardId].szName;
        else
            debug2("Cx2388x-GetCardName: invalid card idx %d (>= %d)", CardId, CX2388xCARD_LASTONE);
    }
    else
        fatal0("Cx2388x-GetCardName: illegal NULL ptr param");

    return pName;
}

static uint GetIffType( TVCARD * pTvCard, bool * pIsPinnacle, bool * pIsMono )
{
    uint  Type = TDA9887_DEFAULT;

    if ((pTvCard != NULL) && (pIsPinnacle != NULL) && (pIsMono != NULL))
    {
        switch (pTvCard->params.cardId)
        {
            case CX2388xCARD_MSI_TV_ANYWHERE_MASTER_PAL:
                Type = TDA9887_MSI_TV_ANYWHERE_MASTER;
                break;
            case TDA9887_LEADTEK_WINFAST_EXPERT:
                Type = TDA9887_LEADTEK_WINFAST_EXPERT;
                break;
            case TDA9887_ATI_TV_WONDER_PRO:
                Type = TDA9887_ATI_TV_WONDER_PRO;
                break;
            case TDA9887_AVERTV_303:
                Type = TDA9887_AVERTV_303;
                break;
            default:
                break;
        }
    }
    else
        fatal0("Cx2388x-GetIffType: illegal NULL ptr param");

    return Type;
}

static uint GetPllType( TVCARD * pTvCard, uint CardId )
{
    if (pTvCard == NULL)
        fatal0("Cx2388x-GetPllType: illegal NULL ptr param");

    return 0;
}

static void GetI2cScanRange( struct TVCARD_struct * pTvCard, uint * pStart, uint * pStop )
{
   if ((pTvCard != NULL) && (pStart != NULL) && (pStop != NULL))
   {
      // check if it's TV@nywhere Master, which has TEA5767 at 0xC0
      if (pTvCard->params.cardId == CX2388xCARD_MSI_TV_ANYWHERE_MASTER_PAL)
         *pStart = 0xC2;
      else
         *pStart = 0xC0;

      *pStop  = 0xCE;
   }
   else
      fatal0("Cx2388x-GetI2cScanRange: illegal NULL ptr param");
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

        if (m_CardType < CX2388xCARD_LASTONE)
        {
            count = m_TVCards[m_CardType].NumInputs;
        }
        else
            debug2("Cx2388x-GetNumInputs: invalid card idx %d (>= %d)", m_CardType, CX2388xCARD_LASTONE);
    }
    else
        fatal0("Cx2388x-GetNumInputs: illegal NULL ptr param");

    return count;
}

static bool IsInputATuner( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    bool  result = FALSE;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < CX2388xCARD_LASTONE) && (nInput < m_TVCards[m_CardType].NumInputs))
        {
            result = (m_TVCards[m_CardType].Inputs[nInput].InputType == INPUTTYPE_TUNER);
        }
        else
            debug4("Cx2388x-IsInputATuner: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CX2388xCARD_LASTONE, nInput, m_TVCards[m_CardType].NumInputs);
    }
    else
        fatal0("Cx2388x-IsInputATuner: illegal NULL ptr param");

    return result;
}

static bool IsInputSVideo( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    bool  result = FALSE;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < CX2388xCARD_LASTONE) && (nInput < m_TVCards[m_CardType].NumInputs))
        {
            result = (m_TVCards[m_CardType].Inputs[nInput].InputType == INPUTTYPE_SVIDEO);
        }
        else
            debug4("Cx2388x-IsInputSVideo: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CX2388xCARD_LASTONE, nInput, m_TVCards[m_CardType].NumInputs);
    }
    else
        fatal0("Cx2388x-IsInputSVideo: illegal NULL ptr param");

    return result;
}

static const char * GetInputName( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    const char * pName = NULL;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < CX2388xCARD_LASTONE) && (nInput < m_TVCards[m_CardType].NumInputs))
        {
           pName = m_TVCards[m_CardType].Inputs[nInput].szName;
        }
        else
            debug4("Cx2388x-GetInputName: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CX2388xCARD_LASTONE, nInput, m_TVCards[m_CardType].NumInputs);
    }
    else
        fatal0("Cx2388x-GetInputName: illegal NULL ptr param");

    return pName;
}

static bool SetVideoSource( TVCARD * pTvCard, uint nInput )
{
    uint  m_CardType;
    bool  result = FALSE;

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        if ((m_CardType < CX2388xCARD_LASTONE) && (nInput < m_TVCards[m_CardType].NumInputs))
        {
            m_TVCards[m_CardType].pInputSwitchFunction(pTvCard, nInput);
            result = TRUE;
        }
        else
            debug4("Cx2388x-SetVideoSource: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CX2388xCARD_LASTONE, nInput, m_TVCards[m_CardType].NumInputs);
    }
    else
        fatal0("Cx2388x-SetVideoSource: illegal NULL ptr param");

    return result;
}

static uint AutoDetectCardType( TVCARD * pTvCard )
{
    DWORD Id;

    if (pTvCard != NULL)
    {
        Id = pTvCard->params.SubSystemId;

        if (Id != 0 && Id != 0xffffffff)
        {
            int i;
            for (i = 0; m_AutoDectect[i].ID != 0; i++)
            {
                if (m_AutoDectect[i].ID  == Id)
                {
                    return m_AutoDectect[i].CardId;
                }
            }
        }
    }
    else
        fatal0("Cx2388x-AutoDetectCardType: illegal NULL ptr param");

    return CX2388xCARD_UNKNOWN;
}

static uint AutoDetectTuner( TVCARD * pTvCard, uint CardId )
{
    if (pTvCard == NULL)
    {
        fatal0("AutoDetectTuner: illegal NULL ptr param");
        return TUNER_ABSENT;
    }

    if(m_TVCards[CardId].TunerId == TUNER_USER_SETUP)
    {
        dprintf0("AutoDetectTuner: auto-detection not supported for this card\n");
        return TUNER_ABSENT;
    }
    else if(m_TVCards[CardId].TunerId == TUNER_AUTODETECT)
    {
        eTunerId Tuner = TUNER_ABSENT;
        int i;
        
        // Read the whole EEPROM
        BYTE Eeprom[256];
        for (i=0; i<256; i += 4)
        {
            // DWORD alignment needed
            DWORD dwVal = ReadDword(MAP_EEPROM_DATA + i);
            Eeprom[i+0] = LOBYTE(LOWORD(dwVal));
            Eeprom[i+1] = HIBYTE(LOWORD(dwVal));
            Eeprom[i+2] = LOBYTE(HIWORD(dwVal));
            Eeprom[i+3] = HIBYTE(HIWORD(dwVal));
        }

        switch(CardId)
        {
          case CX2388xCARD_HAUPPAUGE_PCI_FM:
          case CX2388xCARD_HAUPPAUGE_PCI_FM_TUNERSOUND:
              if (Eeprom[8+0] != 0x84 || Eeprom[8+2] != 0)
              {
                  //Hauppage EEPROM invalid
                  debug2("AutoDetectTuner: Hauppage card. EEPROM error: 0x%02X,0x%02X (!= 0x84,0x00)", Eeprom[8 + 0], Eeprom[8 + 2]);
              }

              else
              {
                  dprintf1("AutoDetectTuner: Hauppage tuner table index %d\n", Eeprom[8+9]);

                  if (Eeprom[8+9] < (sizeof(m_Tuners_Hauppauge_CX2388x_Card) / sizeof(m_Tuners_Hauppauge_CX2388x_Card[0]))) 
                  {
                      Tuner = m_Tuners_Hauppauge_CX2388x_Card[Eeprom[8+9]];
                  }
              }

              break;

          default:
              debug1("AutoDetectTuner: warning: card %d unsupported by tuner auto-detect", CardId);
              break;
        } // switch(CardId)
        
        return Tuner;
    }
    else
    {
        dprintf1("AutoDetectTuner: not necessary, type fixed to %d\n", m_TVCards[CardId].TunerId);
        return m_TVCards[CardId].TunerId;
    }
}

static void StandardInputSelect( TVCARD * pTvCard, uint nInput)
{
    uint  m_CardType = pTvCard->params.cardId;

    if (nInput >= m_TVCards[m_CardType].NumInputs)
    {
        debug1("Input Select Called for invalid input %d", nInput);
        nInput = m_TVCards[m_CardType].NumInputs - 1;
    }

    if(m_TVCards[m_CardType].Inputs[nInput].InputType == INPUTTYPE_COLOURBARS)
    {
        // Enable color bars
        OrDataDword(CX2388X_VIDEO_COLOR_FORMAT, 0x00004000);
    }
    else
    {
        DWORD VideoInput;

        // disable color bars
        AndDataDword(CX2388X_VIDEO_COLOR_FORMAT, 0xFFFFBFFF);
    
        // Read and mask the video input register
        VideoInput = ReadDword(CX2388X_VIDEO_INPUT);
        // zero out mux and svideo bit
        // and force auto detect
        // also turn off CCIR input
        // also VERTEN & SPSPD
        VideoInput &= 0x0F;
        
        // set the Mux up from the card setup
        VideoInput |= (m_TVCards[m_CardType].Inputs[nInput].MuxSelect << CX2388X_VIDEO_INPUT_MUX_SHIFT);

        // set the comp bit for svideo
        switch (m_TVCards[m_CardType].Inputs[nInput].InputType)
        {
            case INPUTTYPE_SVIDEO: // SVideo
                VideoInput |= CX2388X_VIDEO_INPUT_SVID_C_SEL; 
                VideoInput |= CX2388X_VIDEO_INPUT_SVID;

                // Switch chroma DAC to chroma channel
                OrDataDword(MO_AFECFG_IO, 0x00000001);

                break;
            
            case INPUTTYPE_CCIR:
                VideoInput |= CX2388X_VIDEO_INPUT_PE_SRCSEL;
                VideoInput |= CX2388X_VIDEO_INPUT_SVID_C_SEL; 
                break;
        
            case INPUTTYPE_TUNER:
            case INPUTTYPE_COMPOSITE:
            default:
                // Switch chroma DAC to audio
                AndDataDword(MO_AFECFG_IO, 0xFFFFFFFE);
                break;
        }
        
        WriteDword(CX2388X_VIDEO_INPUT, VideoInput);
    }

    // set up any sound stuff
    {
        DWORD dwTemp = m_TVCards[m_CardType].Inputs[nInput].GPIOFlags;
        
        if(dwTemp != 0x00000000)
        {
            // Reset to normal GPIO Mode
            WriteDword(MO_GP3_IO, 0x00000000);
        }

        WriteDword(MO_GP0_IO, dwTemp);
    }
}

static void MSIPalInputSelect( TVCARD * pTvCard, uint nInput)
{
    StandardInputSelect(pTvCard, nInput);
    if(nInput == 0)
    {
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x000040bf);
        WriteDword(MO_GP1_IO, 0x000080c0);
        WriteDword(MO_GP2_IO, 0x0000ff40); 
    }
    else
    {
        // Turn off anything audio if we're not the tuner
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x000040bf);
        WriteDword(MO_GP1_IO, 0x000080c0);
        WriteDword(MO_GP2_IO, 0x0000ff20); 
    }
}

static void PlayHDInputSelect( TVCARD * pTvCard, uint nInput)
{
    StandardInputSelect(pTvCard, nInput);
    if(nInput == 0)
    {
        // GPIO pins set according to values supplied by
        // Tom Fotja
        WriteDword(MO_GP0_IO, 0x0000ff00);
    }
    else
    {
        // Turn off anything audio if we're not the tuner
        WriteDword(MO_GP0_IO, 0x00000ff1);
    }
}

static void AsusInputSelect( TVCARD * pTvCard, uint nInput )
{
    StandardInputSelect(pTvCard, nInput);
    if(nInput == 0)
    {
        // GPIO pins set according to values supplied by
        // Phil Rasmussen 
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x000080ff);
        WriteDword(MO_GP1_IO, 0x000001ff);
        WriteDword(MO_GP2_IO, 0x000000ff); 
    }
    else
    {
        // Turn off anything audio if we're not the tuner
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x0000ff00);
        WriteDword(MO_GP1_IO, 0x0000ff00);
        WriteDword(MO_GP2_IO, 0x0000ff00); 
    }
}

static void LeadtekInputSelect( TVCARD * pTvCard, uint nInput )
{
    StandardInputSelect(pTvCard, nInput);
    if(nInput == 0)
    {
        WriteDword(MO_GP3_IO, 0x02000000); 
        WriteDword(MO_GP0_IO, 0x00F5e700);
        WriteDword(MO_GP1_IO, 0x00003004);
        WriteDword(MO_GP2_IO, 0x00F5e700); 
    }
    
    else
    {
        WriteDword(MO_GP3_IO, 0x02000000); 
        WriteDword(MO_GP0_IO, 0x00F5c700);
        WriteDword(MO_GP1_IO, 0x00003004);
        WriteDword(MO_GP2_IO, 0x00F5c700); 
    }

    // FM-Radio:
    /*
        WriteDword(MO_GP3_IO, 0x02000000); 
        WriteDword(MO_GP0_IO, 0x00F5d700);
        WriteDword(MO_GP1_IO, 0x00003004);
        WriteDword(MO_GP2_IO, 0x00F5d700); 
    */
}

static void AverTV303InputSelect( TVCARD * pTvCard, uint nInput )
{
    StandardInputSelect(pTvCard, nInput);
    if(nInput == 0)
    {
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x000000ff);
        WriteDword(MO_GP1_IO, 0x0000e09f);
        WriteDword(MO_GP2_IO, 0x000000d1); 
    }
    
    else
    {
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x000000ff);
        WriteDword(MO_GP1_IO, 0x0000e05f);
        WriteDword(MO_GP2_IO, 0x000000d1); 
    }

    // muted:
    /*
        WriteDword(MO_GP3_IO, 0x00000000); 
        WriteDword(MO_GP0_IO, 0x000000ff);
        WriteDword(MO_GP1_IO, 0x000020ff);
        WriteDword(MO_GP2_IO, 0x000000d1); 
    */
}

static void StandardSetFormat( TVCARD * pTvCard, uint nInput, eVideoFormat Format, BOOL IsProgressive)
{
    // do nothing
}

// ---------------------------------------------------------------------------
// Fill interface struct
//
static const TVCARD_CFG Cx2388xTyp_Interface =
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

void Cx2388xTyp_GetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      pTvCard->cfg = &Cx2388xTyp_Interface;
   }
   else
      fatal0("Cx2388xTyp-GetInterface: NULL ptr param");
}

