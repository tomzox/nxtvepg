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
 *  DScaler #Id: TunerID.h,v 1.2 2003/02/06 21:27:05 ittarnavsky Exp #
 *  DScaler #Id: TVFormats.h,v 1.6 2003/01/07 16:49:08 adcockj Exp #
 *
 *  $Id: wintuner.h,v 1.7 2003/02/22 14:58:33 tom Exp tom $
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
// Interface declaration
//
bool Tuner_Init( TUNER_TYPE type, TVCARD * pTvCardIf );
void Tuner_Close( void );
bool Tuner_SetFrequency( TUNER_TYPE type, uint wFrequency, uint norm );
const char * Tuner_GetName( uint idx );

#endif  // __WINTUNER_H
