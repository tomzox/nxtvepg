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
 *  Author: Tom Zoerner
 *
 *  $Id: wintuner.h,v 1.1 2002/11/26 19:13:32 tom Exp tom $
 */

#ifndef __WINTUNER_H
#define __WINTUNER_H

// ---------------------------------------------------------------------------
// Declarations for tuner control, copied from bttv tuner driver
// Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
//
typedef enum
{  // must be the same order as in the table!
   TUNER_NONE,
   TUNER_TEMIC_PAL,
   TUNER_PHILIPS_PAL_I,
   TUNER_PHILIPS_NTSC,
   TUNER_PHILIPS_SECAM,
   TUNER_PHILIPS_PAL,
   TUNER_TEMIC_NTSC,
   TUNER_TEMIC_PAL_I,
   TUNER_TEMIC_4036FY5_NTSC,
   TUNER_ALPS_TSBH1_NTSC,
   TUNER_ALPS_TSBE1_PAL,
   TUNER_ALPS_TSBB5_PAL_I,
   TUNER_ALPS_TSBE5_PAL,
   TUNER_ALPS_TSBC5_PAL,
   TUNER_TEMIC_4006FH5_PAL,
   TUNER_ALPS_TSHC6_NTSC,
   TUNER_TEMIC_PAL_DK,
   TUNER_PHILIPS_NTSC_M,
   TUNER_TEMIC_4066FY5_PAL_I,
   TUNER_TEMIC_4006FN5_MULTI_PAL,
   TUNER_TEMIC_4009FR5_PAL,
   TUNER_TEMIC_4039FR5_NTSC,
   TUNER_TEMIC_4046FM5,
   TUNER_PHILIPS_PAL_DK,
   TUNER_PHILIPS_FQ1216ME,
   TUNER_LG_PAL_I_FM,
   TUNER_LG_PAL_I,
   TUNER_LG_NTSC_FM,
   TUNER_LG_PAL_FM,
   TUNER_LG_PAL,
   TUNER_TEMIC_4009FN5_MULTI_PAL_FM,
   TUNER_SHARP_2U5JF5540_NTSC,
   TUNER_Samsung_PAL_TCPM9091PD27,
   TUNER_MT2032,
   TUNER_TEMIC_4106FH5,
   TUNER_TEMIC_4012FY5,
   TUNER_TEMIC_4136FY5,
   TUNER_LG_PAL_NEW_TAPC,
   TUNER_PHILIPS_FM1216ME_MK3,
   TUNER_LG_NTSC_NEW_TAPC,
   TUNER_COUNT
} TUNER_TYPE;

// ---------------------------------------------------------------------------
// Interface declaration
//
bool Tuner_Init( TUNER_TYPE type );
void Tuner_Close( void );
bool Tuner_SetFrequency( TUNER_TYPE type, uint wFrequency, uint norm );
const char * Tuner_GetName( uint idx );
uint Tuner_MatchByParams( uint thresh1, uint thresh2,
                          uchar VHF_L, uchar VHF_H, uchar UHF,
                          uchar config, ushort IFPCoff );

#endif  // __WINTUNER_H
