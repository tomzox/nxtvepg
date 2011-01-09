/*
 *  Tuner driver module
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
 *  Author: see C source file.
 *
 *  DScaler #Id: TunerID.h,v 1.12 2005/08/11 17:21:55 to_see Exp #
 *  DScaler #Id: TVFormats.h,v 1.6 2003/01/07 16:49:08 adcockj Exp #
 *  DScaler #Id: TDA9887.h,v 1.8 2004/09/29 20:36:02 to_see Exp #
 *
 *  $Id: wintuner.h,v 1.12 2011/01/05 19:26:28 tom Exp tom $
 */

#ifndef __WINTUNER_H
#define __WINTUNER_H

// ---------------------------------------------------------------------------
// Declarations for tuner control, originally copied from bttv tuner driver
// - Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
// - later replaced with DScaler tuner table
//
typedef enum
{  // must be the same order as in the table!
    TUNER_AUTODETECT = -2,
    TUNER_USER_SETUP = -1,
    TUNER_ABSENT = 0,           
    TUNER_PHILIPS_PAL_I,        
    TUNER_PHILIPS_NTSC,     
    TUNER_PHILIPS_SECAM,        
    TUNER_PHILIPS_PAL,      
    TUNER_TEMIC_4002FH5_PAL,
    TUNER_TEMIC_4032FY5_NTSC,       
    TUNER_TEMIC_4062FY5_PAL_I,      
    TUNER_TEMIC_4036FY5_NTSC,
    TUNER_ALPS_TSBH1_NTSC,  
    TUNER_ALPS_TSBE1_PAL,   
    TUNER_ALPS_TSBB5_PAL_I,     
    TUNER_ALPS_TSBE5_PAL,   
    TUNER_ALPS_TSBC5_PAL,   
    TUNER_TEMIC_4006FH5_PAL,
    TUNER_PHILIPS_1236D_NTSC_INPUT1,
    TUNER_PHILIPS_1236D_NTSC_INPUT2,
    TUNER_ALPS_TSCH6_NTSC,
    TUNER_TEMIC_4016FY5_PAL,
    TUNER_PHILIPS_MK2_NTSC,
    TUNER_TEMIC_4066FY5_PAL_I,
    TUNER_TEMIC_4006FN5_PAL,
    TUNER_TEMIC_4009FR5_PAL,
    TUNER_TEMIC_4039FR5_NTSC,
    TUNER_TEMIC_4046FM5_MULTI,
    TUNER_PHILIPS_PAL_DK,
    TUNER_PHILIPS_MULTI,
    TUNER_LG_I001D_PAL_I,
    TUNER_LG_I701D_PAL_I,
    TUNER_LG_R01F_NTSC,
    TUNER_LG_B01D_PAL,
    TUNER_LG_B11D_PAL,
    TUNER_TEMIC_4009FN5_PAL,
    TUNER_MT2032,
    TUNER_SHARP_2U5JF5540_NTSC,
    TUNER_LG_TAPCH701P_NTSC,
    TUNER_SAMSUNG_PAL_TCPM9091PD27,
    TUNER_TEMIC_4106FH5,
    TUNER_TEMIC_4012FY5,
    TUNER_TEMIC_4136FY5,
    TUNER_LG_TAPCNEW_PAL,
    TUNER_PHILIPS_FM1216ME_MK3,
    TUNER_LG_TAPCNEW_NTSC,
    TUNER_MT2032_PAL,
    TUNER_PHILIPS_FI1286_NTSC_M_J,
    TUNER_MT2050,
    TUNER_MT2050_PAL,
    TUNER_PHILIPS_4IN1,
    TUNER_TCL_2002N,
    TUNER_HITACHI_NTSC,
    TUNER_PHILIPS_PAL_MK,
    TUNER_PHILIPS_FM1236_MK3,
    TUNER_LG_NTSC_TAPE,
    TUNER_TNF_8831BGFF,
    TUNER_PHILIPS_FM1256_IH3,
    TUNER_PHILIPS_FQ1286,
    TUNER_LG_PAL_TAPE,
    TUNER_PHILIPS_FQ1216AME_MK4,
    TUNER_PHILIPS_FQ1236A_MK4,
    TUNER_TDA8275,
    TUNER_YMEC_TVF_8531MF,
    TUNER_YMEC_TVF_5533MF,
    TUNER_TENA_9533_DI,
    TUNER_PHILIPS_FMD1216ME_MK3,
    TUNER_LASTONE,
} eTunerId;

#define TUNERS_COUNT  TUNER_LASTONE

typedef eTunerId TUNER_TYPE;

typedef enum
{
    VIDEOFORMAT_PAL_B = 0,
    VIDEOFORMAT_PAL_D,
    VIDEOFORMAT_PAL_G,
    VIDEOFORMAT_PAL_H,
    VIDEOFORMAT_PAL_I,
    VIDEOFORMAT_PAL_M,
    VIDEOFORMAT_PAL_N,
    VIDEOFORMAT_PAL_60,
    VIDEOFORMAT_PAL_N_COMBO,

    VIDEOFORMAT_SECAM_B,
    VIDEOFORMAT_SECAM_D,
    VIDEOFORMAT_SECAM_G,
    VIDEOFORMAT_SECAM_H,
    VIDEOFORMAT_SECAM_K,
    VIDEOFORMAT_SECAM_K1,
    VIDEOFORMAT_SECAM_L,
    VIDEOFORMAT_SECAM_L1,

    VIDEOFORMAT_NTSC_M,
    VIDEOFORMAT_NTSC_M_Japan,
    VIDEOFORMAT_NTSC_50,

    VIDEOFORMAT_LASTONE
} eVideoFormat;

// ---------------------------------------------------------------------------
// from TDA9887.h

// for CTDA9887Ex

// TV format groupings used by TDA9887
typedef enum
{
    TDA9887_FORMAT_NONE     = -1,
    TDA9887_FORMAT_PAL_BG   = 0,
    TDA9887_FORMAT_PAL_I,
    TDA9887_FORMAT_PAL_DK,
    TDA9887_FORMAT_PAL_MN,
    TDA9887_FORMAT_SECAM_L,
    TDA9887_FORMAT_SECAM_DK,
    TDA9887_FORMAT_NTSC_M,
    TDA9887_FORMAT_NTSC_JP,
    TDA9887_FORMAT_RADIO,
    TDA9887_FORMAT_LASTONE,
} eTDA9887Format;

// Bits for SetCardSpecifics(...)'s TTDA9887CardSpecifics
enum
{
    TDA9887_SM_CARRIER_QSS              = 0x20, // != Intercarrier
    TDA9887_SM_OUTPUTPORT1_INACTIVE     = 0x40, // != Active
    TDA9887_SM_OUTPUTPORT2_INACTIVE     = 0x80, // != Active
    TDA9887_SM_TAKEOVERPOINT_MASK       = 0x1F,
    TDA9887_SM_TAKEOVERPOINT_OFFSET     = 0,

    TDA9887_SM_TAKEOVERPOINT_DEFAULT    = 0x10, // 0 dB
    TDA9887_SM_TAKEOVERPOINT_MIN        = 0x00, // -16 dB
    TDA9887_SM_TAKEOVERPOINT_MAX        = 0x1F, // +15 dB
};

// Only the modes specified in the enum above can be changed with
// SetModes(...).  To change a mode, add the respective constant to the
// 'mask' value then specify the new value in 'bits'.  For example, to
// use the QSS carrier mode and active OutputPort2 mode, the
// following will be used:
//
// mask = TDA9887_SM_CARRIER_QSS|TDA9887_SM_OUTPUTPORT2_INACTIVE;
// value = TDA9887_SM_CARRIER_QSS;
//
// If no changes are made, modes listed in k_TDAStandardtSettings are
// used.

// Input structure for SetModes(...).
typedef struct
{
    BYTE    mask;
    BYTE    bits;
} TTDA9887Modes;

// Input structure for SetModes(...).
struct TTDA9887FormatModes_s
{
    eTDA9887Format  format;
    BYTE            mask;
    BYTE            bits;
};

// ---------------------------------------------------------------------------
// Interface declaration
//
bool Tuner_Init( TUNER_TYPE type, TVCARD * pTvCardIf );
void Tuner_Close( void );
bool Tuner_SetFrequency( TUNER_TYPE type, uint wFrequency, uint norm );
const char * Tuner_GetName( uint idx );
eTunerId Tuner_GetHauppaugeEepromId( uint idx );

#endif  // __WINTUNER_H
