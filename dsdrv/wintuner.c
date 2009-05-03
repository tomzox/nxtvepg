/*
 *  M$ Windows tuner driver module
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
 *    This module is a "user-space driver" for various tuner chips used on
 *    PCI tuner cards.  The code is more or less directly copied from the
 *    BTTV Linux driver by Gerd Knorr et.al. and was originally adapted
 *    to WinDriver by Espresso, cleaned up by Tom Zoerner, adapted for
 *    DSdrv by e-nek, then further updated by merging with BTTV and DScaler.
 *
 *  Authors:
 *
 *      Copyright (C) 1997 Markus Schroeder (schroedm@uni-duesseldorf.de)
 *      Copyright (C) 1997,1998 Gerd Knorr (kraxel@goldbach.in-berlin.de)
 *      also Ralph Metzler, Gunther Mayer and others; see also btdrv4win.c
 *      DScaler parts are copyleft 2001-2003 itt@myself.com
 *
 *  DScaler #Id: GenericTuner.cpp,v 1.21 2005/07/17 15:58:28 to_see Exp #
 *  DScaler #Id: TunerID.cpp,v 1.10 2005/07/17 15:58:28 to_see Exp #
 *  DScaler #Id: TunerID.h,v 1.12 2005/08/11 17:21:55 to_see Exp #
 *  DScaler #Id: MT2032.cpp,v 1.13 2004/01/14 17:06:44 robmuller Exp #
 *  DScaler #Id: MT2050.cpp,v 1.5 2004/04/06 12:20:48 adcockj Exp #
 *  DScaler #Id: TDA9887.cpp,v 1.20 2005/03/09 13:19:15 atnak Exp #
 *  DScaler #Id: SAA7134Card_Tuner.cpp,v 1.21 2005/03/09 15:20:04 atnak Exp #
 *  DScaler #Id: TEA5767.cpp,v 1.2 2005/08/07 09:43:27 to_see Exp #
 *  DScaler #Id: TDA8290.cpp,v 1.7 2007/08/12 17:42:25 dosx86 Exp #
 *  DScaler #Id: TDA8290.h,v 1.4 2005/03/09 13:19:51 atnak Exp #
 *  DScaler #Id: TDA8275.cpp,v 1.10 2005/10/04 19:59:48 to_see Exp #
 *  DScaler #Id: TDA8275.h,v 1.6 2005/10/04 19:59:09 to_see Exp #
 *
 *  $Id: wintuner.c,v 1.29 2009/04/25 17:54:40 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "epgvbi/btdrv.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/dsdrvlib.h"
#include "dsdrv/wintuner.h"


// ---------------------------------------------------------------------------
// Module state variables

static BYTE TunerDeviceI2C = 0;
static uint MicrotuneType;
static bool haveTda9887Standard;
static bool haveTda9887Pinnacle;
static bool isTda9887PinnacleMono;
static bool isTda9887Ex;
static bool haveTda8290;
static BYTE Tda9887DeviceI2C = 0;
static uint m_LastVideoFormat = 0xff;
static TVCARD * pTvCard = NULL;

// ---------------------------------------------------------------------------
// Tuner table
//
static const char * TunerNames[TUNER_LASTONE] =
{
    // Name                                         v4l2    ID
    "*No Tuner/Unknown*",                       //    4     TUNER_ABSENT = 0
    "Philips FI1246 [PAL I]",                   //    1     TUNER_PHILIPS_PAL_I     
    "Philips FI1236 [NTSC]",                    //    2     TUNER_PHILIPS_NTSC      
    "Philips [SECAM]",                          //    3     TUNER_PHILIPS_SECAM     
    "Philips [PAL]",                            //    5     TUNER_PHILIPS_PAL
    "Temic 4002 FH5 [PAL B/G]",                 //    0     TUNER_TEMIC_4002FH5_PAL
    "Temic 4032 FY5 [NTSC]",                    //    6     TUNER_TEMIC_4032FY5_NTSC
    "Temic 4062 FY5 [PAL I]",                   //    7     TUNER_TEMIC_4062FY5_PAL_I
    "Temic 4036 FY5 [NTSC]",                    //    8     TUNER_TEMIC_4036FY5_NTSC        
    "Alps TSBH1 [NTSC]",                        //    9     TUNER_ALPS_TSBH1_NTSC                             
    "Alps TSBE1 [PAL]",                         //   10     TUNER_ALPS_TSBE1_PAL                                    
    "Alps TSBB5 [PAL I]",                       //   11     TUNER_ALPS_TSBB5_PAL_I                                  
    "Alps TSBE5 [PAL]",                         //   12     TUNER_ALPS_TSBE5_PAL                                    
    "Alps TSBC5 [PAL]",                         //   13     TUNER_ALPS_TSBC5_PAL                                    
    "Temic 4006 FH5 [PAL B/G]",                 //   14     TUNER_TEMIC_4006FH5_PAL     
    "Philips 1236D Input 1 [ATSC/NTSC]",        //   42     TUNER_PHILIPS_1236D_NTSC_INPUT1
    "Philips 1236D Input 2 [ATSC/NTSC]",        //   42     TUNER_PHILIPS_1236D_NTSC_INPUT2
    "Alps TSCH6 [NTSC]",                        //   15     TUNER_ALPS_TSCH6_NTSC                                      
    "Temic 4016 FY5 [PAL D/K/L]",               //   16     TUNER_TEMIC_4016FY5_PAL
    "Philips MK2 [NTSC M]",                     //   17     TUNER_PHILIPS_MK2_NTSC      
    "Temic 4066 FY5 [PAL I]",                   //   18     TUNER_TEMIC_4066FY5_PAL_I
    "Temic 4006 FN5 [PAL Auto]",                //   19     TUNER_TEMIC_4006FN5_PAL
    "Temic 4009 FR5 [PAL B/G] + FM",            //   20     TUNER_TEMIC_4009FR5_PAL
    "Temic 4039 FR5 [NTSC] + FM",               //   21     TUNER_TEMIC_4039FR5_NTSC
    "Temic 4046 FM5 [PAL/SECAM multi]",         //   22     TUNER_TEMIC_4046FM5_MULTI
    "Philips [PAL D/K]",                        //   23     TUNER_PHILIPS_PAL_DK
    "Philips FQ1216ME [PAL/SECAM multi]",       //   24     TUNER_PHILIPS_MULTI     
    "LG TAPC-I001D [PAL I] + FM",               //   25     TUNER_LG_I001D_PAL_I
    "LG TAPC-I701D [PAL I]",                    //   26     TUNER_LG_I701D_PAL_I
    "LG TPI8NSR01F [NTSC] + FM",                //   27     TUNER_LG_R01F_NTSC
    "LG TPI8PSB01D [PAL B/G] + FM",             //   28     TUNER_LG_B01D_PAL
    "LG TPI8PSB11D [PAL B/G]",                  //   29     TUNER_LG_B11D_PAL       
    "Temic 4009 FN5 [PAL Auto] + FM",           //   30     TUNER_TEMIC_4009FN5_PAL
    "MT2032 universal [SECAM default]",         //   33     TUNER_MT2032
    "Sharp 2U5JF5540 [NTSC JP]",                //   31     TUNER_SHARP_2U5JF5540_NTSC
    "LG TAPC-H701P [NTSC]",                     //          TUNER_LG_TAPCH701P_NTSC
    "Samsung TCPM9091PD27 [PAL B/G/I/D/K]",     //   32     TUNER_SAMSUNG_PAL_TCPM9091PD27
    "Temic 4106 FH5 [PAL B/G]",                 //   34     TUNER_TEMIC_4106FH5  
    "Temic 4012 FY5 [PAL D/K/L]",               //   35     TUNER_TEMIC_4012FY5     
    "Temic 4136 FY5 [NTSC]",                    //   36     TUNER_TEMIC_4136FY5
    "LG TAPC-new [PAL]",                        //   37     TUNER_LG_TAPCNEW_PAL        
    "Philips FQ1216ME MK3 [PAL/SECAM multi]",   //   38     TUNER_PHILIPS_FM1216ME_MK3
    "LG TAPC-new [NTSC]",                       //   39     TUNER_LG_TAPCNEW_NTSC
    "MT2032 universal [PAL default]",           //   33     TUNER_MT2032_PAL
    "Philips FI1286 [NTCS M/J]",                //          TUNER_PHILIPS_FI1286_NTSC_M_J
    "MT2050 [SECAM default]",                   //   33     TUNER_MT2050
    "MT2050 [PAL default]",                     //   33     TUNER_MT2050_PAL
    "Philips 4in1 [ATI TV Wonder Pro/Conexant]",//   44     TUNER_PHILIPS_4IN1
    "TCL 2002N",                                //   50     TUNER_TCL_2002N
    "HITACHI V7-J180AT",                        //   40     TUNER_HITACHI_NTSC
    "Philips FI1216 MK [PAL]",                  //   41     TUNER_PHILIPS_PAL_MK
    "Philips FM1236 MK3 [NTSC]",                //   43     TUNER_PHILIPS_FM1236_MK3
    "LG TAPE series [NTSC]",                    //   47     TUNER_LG_NTSC_TAPE
    "Tenna TNF 8831 [PAL]",                     //   48     TUNER_TNF_8831BGFF
    "Philips FM1256 MK3 [PAL/SECAM D]",         //   51     TUNER_PHILIPS_FM1256_IH3
    "Philips FQ1286 [NTSC]",                    //   53     TUNER_PHILIPS_FQ1286
    "LG TAPE series [PAL]",                     //   55     TUNER_LG_PAL_TAPE
    "Philips FM1216AME [PAL/SECAM multi]",      //   56     TUNER_PHILIPS_FQ1216AME_MK4
    "Philips FQ1236A MK4 [NTSC]",               //   57     TUNER_PHILIPS_FQ1236A_MK4
    "Philips TDA8275",                          //   54     TUNER_TDA8275
    "Ymec TVF-8531MF/8831MF/8731MF [NTSC]",     //   58     TUNER_YMEC_TVF_8531MF
    "Ymec TVision TVF-5533MF [NTSC]",           //   59     TUNER_YMEC_TVF_5533MF
    "Tena TNF9533-D/IF/TNF9533-B/DF [PAL]",     //   61     TUNER_TENA_9533_DI
    "Philips FMD1216ME MK3 Hybrid [PAL]",       //   63     TUNER_PHILIPS_FMD1216ME_MK3
};

// ---------------------------------------------------------------------------

/* tv standard selection for Temic 4046 FM5
   this value takes the low bits of control byte 2
   from datasheet Rev.01, Feb.00 
     standard     BG      I       L       L2      D
     picture IF   38.9    38.9    38.9    33.95   38.9
     sound 1      33.4    32.9    32.4    40.45   32.4
     sound 2      33.16   
     NICAM        33.05   32.348  33.05           33.05
 */

#define TEMIC_SET_PAL_I         0x05
#define TEMIC_SET_PAL_DK        0x09
#define TEMIC_SET_PAL_L         0x0a // SECAM ?
#define TEMIC_SET_PAL_L2        0x0b // change IF !
#define TEMIC_SET_PAL_BG        0x0c

/* tv tuner system standard selection for Philips FQ1216ME
   this value takes the low bits of control byte 2
   from datasheet "1999 Nov 16" (supersedes "1999 Mar 23")
     standard 		BG	DK	I	L	L`
     picture carrier	38.90	38.90	38.90	38.90	33.95
     colour		34.47	34.47	34.47	34.47	38.38
     sound 1		33.40	32.40	32.90	32.40	40.45
     sound 2		33.16	-	-	-	-
     NICAM		33.05	33.05	32.35	33.05	39.80
 */
#define PHILIPS_SET_PAL_I		0x01 /* Bit 2 always zero !*/
#define PHILIPS_SET_PAL_BGDK	0x09
#define PHILIPS_SET_PAL_L2		0x0a
#define PHILIPS_SET_PAL_L		0x0b	

/* system switching for Philips FI1216MF MK2
   from datasheet "1996 Jul 09",
 */
#define PHILIPS_MF_SET_BG		0x01 /* Bit 2 must be zero, Bit 3 is system output */
#define PHILIPS_MF_SET_PAL_L	0x03
#define PHILIPS_MF_SET_PAL_L2	0x02

#define MT2032 0x04
#define MT2030 0x06
#define MT2040 0x07
#define MT2050 0x42


typedef struct
{
   eTunerId     tunerId;
   eVideoFormat videoFormat;
   WORD         thresh1;
   WORD         thresh2;  
   BYTE         vhf_l;
   BYTE         vhf_h;
   BYTE         uhf;
   BYTE         config; 
   WORD         IfpCoff;
} TTunerType;

#define TUNERDEF(TID,VFMT,T1,T2,VHFL,VHFH,UHF,CFG,IFPC) \
        pTuner->tunerId = (TID); \
        pTuner->videoFormat = (VFMT); \
        pTuner->thresh1 = (T1); \
        pTuner->thresh2 = (T2); \
        pTuner->vhf_l = (VHFL); \
        pTuner->vhf_h = (VHFH); \
        pTuner->uhf = (UHF); \
        pTuner->config = (CFG); \
        pTuner->IfpCoff = (IFPC)

static void Tuner_GetParams( eTunerId tunerId, TTunerType * pTuner )
{
    switch (tunerId)
    {
       default:
           {
               TUNERDEF(TUNER_ABSENT, VIDEOFORMAT_NTSC_M,
                   0,0,0,0,0,0,0);
               break;
           }
       case TUNER_PHILIPS_PAL_I:
           { 
               TUNERDEF(TUNER_PHILIPS_PAL_I, VIDEOFORMAT_PAL_I, 
                   16*140.25, 16*463.25, 0xa0, 0x90, 0x30, 0x8e, 623);
               break;
           }
       case TUNER_PHILIPS_NTSC:
           { 
               TUNERDEF(TUNER_PHILIPS_NTSC, VIDEOFORMAT_NTSC_M, 
                   16*157.25, 16*451.25, 0xA0, 0x90, 0x30, 0x8e, 732);
               break;
           }
       case TUNER_PHILIPS_SECAM:
           { 
               TUNERDEF(TUNER_PHILIPS_SECAM, VIDEOFORMAT_SECAM_D, 
                   16*168.25, 16*447.25, 0xA7, 0x97, 0x37, 0x8e, 623);
               break;
           }
       case TUNER_PHILIPS_PAL:
           { 
               TUNERDEF(TUNER_PHILIPS_PAL, VIDEOFORMAT_PAL_B, 
                   16*168.25, 16*447.25, 0xA0, 0x90, 0x30, 0x8e, 623);
               break;
           }
       case TUNER_TEMIC_4002FH5_PAL:
           { 
               TUNERDEF(TUNER_TEMIC_4002FH5_PAL, VIDEOFORMAT_PAL_B,
                   16*140.25, 16*463.25, 0x02, 0x04, 0x01, 0x8e, 623);
               break;
           }
       case TUNER_TEMIC_4032FY5_NTSC:
           {
               TUNERDEF(TUNER_TEMIC_4032FY5_NTSC, VIDEOFORMAT_NTSC_M, 
                   16*157.25, 16*463.25, 0x02, 0x04, 0x01, 0x8e, 732);
               break;
           }
       case TUNER_TEMIC_4062FY5_PAL_I:
           {
               TUNERDEF(TUNER_TEMIC_4062FY5_PAL_I, VIDEOFORMAT_PAL_I, 
                   16*170.00, 16*450.00, 0x02, 0x04, 0x01, 0x8e, 623);
               break;
           }
       case TUNER_TEMIC_4036FY5_NTSC:
           {
               TUNERDEF(TUNER_TEMIC_4036FY5_NTSC, VIDEOFORMAT_NTSC_M, 
                   16*157.25, 16*463.25, 0xa0, 0x90, 0x30, 0x8e, 732);
               break;
           }
       case TUNER_ALPS_TSBH1_NTSC:
           {
               TUNERDEF(TUNER_ALPS_TSBH1_NTSC, VIDEOFORMAT_NTSC_M, 
                   16*137.25, 16*385.25, 0x01, 0x02, 0x08, 0x8e, 732);
               break;
           }
       case TUNER_ALPS_TSBE1_PAL:
           {
               TUNERDEF(TUNER_ALPS_TSBE1_PAL, VIDEOFORMAT_PAL_B, 
                   16*137.25, 16*385.25, 0x01, 0x02, 0x08, 0x8e, 732);
               break;
           }
       case TUNER_ALPS_TSBB5_PAL_I:
           {
               TUNERDEF(TUNER_ALPS_TSBB5_PAL_I, VIDEOFORMAT_PAL_I, 
                   16*133.25, 16*351.25, 0x01, 0x02, 0x08, 0x8e, 632);
               break;
           }
       case TUNER_ALPS_TSBE5_PAL:
           {
               TUNERDEF(TUNER_ALPS_TSBE5_PAL, VIDEOFORMAT_PAL_B, 
                   16*133.25, 16*351.25, 0x01, 0x02, 0x08, 0x8e, 622);
               break;
           }
       case TUNER_ALPS_TSBC5_PAL:
           {
               TUNERDEF(TUNER_ALPS_TSBC5_PAL, VIDEOFORMAT_PAL_B, 
                   16*133.25, 16*351.25, 0x01, 0x02, 0x08, 0x8e, 608);
               break;
           }
       case TUNER_TEMIC_4006FH5_PAL:
           {
               TUNERDEF(TUNER_TEMIC_4006FH5_PAL, VIDEOFORMAT_PAL_B, 
                   16*170.00,16*450.00, 0xa0, 0x90, 0x30, 0x8e, 623);
               break;
           }
       case TUNER_PHILIPS_1236D_NTSC_INPUT1:
           {
               TUNERDEF(TUNER_PHILIPS_1236D_NTSC_INPUT1, VIDEOFORMAT_NTSC_M, 
                   2516, 7220, 0xA3, 0x93, 0x33, 0xCE, 732);
               break;
           }
       case TUNER_PHILIPS_1236D_NTSC_INPUT2:
           {
               TUNERDEF(TUNER_PHILIPS_1236D_NTSC_INPUT2, VIDEOFORMAT_NTSC_M, 
                   2516, 7220, 0xA2, 0x92, 0x32, 0xCE, 732);
               break;
           }
       case TUNER_ALPS_TSCH6_NTSC:
           {
               TUNERDEF(TUNER_ALPS_TSCH6_NTSC, VIDEOFORMAT_NTSC_M,
                   16*137.25, 16*385.25, 0x14, 0x12, 0x11, 0x8e, 732);
               break;
           }
       case TUNER_TEMIC_4016FY5_PAL:
           {
               TUNERDEF(TUNER_TEMIC_4016FY5_PAL, VIDEOFORMAT_PAL_B,
                   16*136.25, 16*456.25, 0xa0, 0x90, 0x30, 0x8e, 623);
               break;
           }
       case TUNER_PHILIPS_MK2_NTSC:
           {
               TUNERDEF(TUNER_PHILIPS_MK2_NTSC, VIDEOFORMAT_NTSC_M,
                   16*160.00,16*454.00,0xa0,0x90,0x30,0x8e,732);
               break;
           }
       case TUNER_TEMIC_4066FY5_PAL_I:
           {
               TUNERDEF(TUNER_TEMIC_4066FY5_PAL_I, VIDEOFORMAT_PAL_I,
                   16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_TEMIC_4006FN5_PAL:
           {
               TUNERDEF(TUNER_TEMIC_4006FN5_PAL, VIDEOFORMAT_PAL_B,
                   16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_TEMIC_4009FR5_PAL:
           { 
               TUNERDEF(TUNER_TEMIC_4009FR5_PAL, VIDEOFORMAT_PAL_B,
                   16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_TEMIC_4039FR5_NTSC:
           {
               TUNERDEF(TUNER_TEMIC_4039FR5_NTSC, VIDEOFORMAT_NTSC_M,
                   16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732);
               break;
           }
       case TUNER_TEMIC_4046FM5_MULTI:
           { 
               TUNERDEF(TUNER_TEMIC_4046FM5_MULTI, VIDEOFORMAT_PAL_B,
                   16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_PHILIPS_PAL_DK:
           { 
               TUNERDEF(TUNER_PHILIPS_PAL_DK, VIDEOFORMAT_PAL_D,
                   16*170.00, 16*450.00, 0xa0, 0x90, 0x30, 0x8e, 623);
               break;
           }
       case TUNER_PHILIPS_MULTI:
           { 
               TUNERDEF(TUNER_PHILIPS_MULTI, VIDEOFORMAT_PAL_B,
                   16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_LG_I001D_PAL_I:
           { 
               TUNERDEF(TUNER_LG_I001D_PAL_I, VIDEOFORMAT_PAL_I,
                   16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_LG_I701D_PAL_I:
           { 
               TUNERDEF(TUNER_LG_I701D_PAL_I, VIDEOFORMAT_PAL_I,
                   16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_LG_R01F_NTSC:
           { 
               TUNERDEF(TUNER_LG_R01F_NTSC, VIDEOFORMAT_NTSC_M,
                   16*210.00,16*497.00,0xa0,0x90,0x30,0x8e,732);
               break;
           }
       case TUNER_LG_B01D_PAL:
           { 
               TUNERDEF(TUNER_LG_B01D_PAL, VIDEOFORMAT_PAL_B,
                   16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_LG_B11D_PAL:
           { 
               TUNERDEF(TUNER_LG_B11D_PAL, VIDEOFORMAT_PAL_B,
                   16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_TEMIC_4009FN5_PAL:
           { 
               TUNERDEF(TUNER_TEMIC_4009FN5_PAL, VIDEOFORMAT_PAL_B,
                   16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623);
               break;
           }
       case TUNER_SHARP_2U5JF5540_NTSC:
           {
               TUNERDEF(TUNER_SHARP_2U5JF5540_NTSC, VIDEOFORMAT_NTSC_M_Japan,
                   16*137.25, 16*317.25, 0x01, 0x02, 0x08, 0x8e, 940);
               break;
           }
       case TUNER_LG_TAPCH701P_NTSC:
           {
               TUNERDEF(TUNER_LG_TAPCH701P_NTSC, VIDEOFORMAT_NTSC_M, 
                   16*165.00, 16*450.00, 0x01, 0x02, 0x08, 0x8e, 732);
               break;
           }
       case TUNER_SAMSUNG_PAL_TCPM9091PD27:
         {
             TUNERDEF(TUNER_SAMSUNG_PAL_TCPM9091PD27, VIDEOFORMAT_PAL_I,
                 16*(169),16*(464),0xA0,0x90,0x30,0x8e,623);
             break;
         }
       case TUNER_TEMIC_4106FH5:
         {
             TUNERDEF(TUNER_TEMIC_4106FH5, VIDEOFORMAT_PAL_B,
                 16*(141.00),16*(464.00),0xa0,0x90,0x30,0x8e,623);
             break;
         }
       case TUNER_TEMIC_4012FY5:
         {
             TUNERDEF(TUNER_TEMIC_4012FY5, VIDEOFORMAT_SECAM_D,
                 16*(140.25),16*(460.00),0x02,0x04,0x01,0x8e,608);
             break;
         }
       case TUNER_TEMIC_4136FY5:
         {
             TUNERDEF(TUNER_TEMIC_4136FY5, VIDEOFORMAT_NTSC_M,
                 16*(158.00),16*(453.00),0xa0,0x90,0x30,0x8e,732);
             break;
         }
       case TUNER_LG_TAPCNEW_PAL:
         {
             TUNERDEF(TUNER_LG_TAPCNEW_PAL, VIDEOFORMAT_PAL_B,
                 16*(170.00),16*(450.00),0x01,0x02,0x08,0x8e,623);
             break;
         }
       case TUNER_PHILIPS_FM1216ME_MK3:
         {
             TUNERDEF(TUNER_PHILIPS_FM1216ME_MK3, VIDEOFORMAT_PAL_B,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0x8e,623);
             break;
         }
       case TUNER_LG_TAPCNEW_NTSC:
         {
             TUNERDEF(TUNER_LG_TAPCNEW_NTSC, VIDEOFORMAT_NTSC_M,
                 16*(170.00),16*(450.00),0x01,0x02,0x08,0x8e,732);
             break;
         }
       case TUNER_PHILIPS_FI1286_NTSC_M_J:
         {
           //  { "Philips FI1286", Philips, NTSC, 16*160.00, 16*454.00, 0x01, 0x02, 0x04, 0x8e, 940},
             TUNERDEF(TUNER_PHILIPS_FI1286_NTSC_M_J, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(454.00),0x01,0x02,0x04,0x8e,940);
             break;
         }
       case TUNER_PHILIPS_4IN1:
         {
             TUNERDEF(TUNER_PHILIPS_4IN1, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0x8e,732);
             break;
         }
       case TUNER_TCL_2002N:
         {
             TUNERDEF(TUNER_TCL_2002N, VIDEOFORMAT_NTSC_M,
                 16*(172.00),16*(448.00),0x01,0x02,0x08,0x8e,732);
             break;
         }
       case TUNER_HITACHI_NTSC:
         {
             TUNERDEF(TUNER_HITACHI_NTSC, VIDEOFORMAT_NTSC_M,
                 16*(170.00),16*(450.00),0x01,0x02,0x08,0x8e,940);
             break;
         }
       case TUNER_PHILIPS_PAL_MK:
         {
             TUNERDEF(TUNER_PHILIPS_PAL_MK, VIDEOFORMAT_PAL_B,
                 16*(140.00),16*(463.25),0x01,0xc2,0xcf,0x8e,623);
             break;
         }
       case TUNER_PHILIPS_FM1236_MK3:
         {
             TUNERDEF(TUNER_PHILIPS_FM1236_MK3, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0x8e,732);
             break;
         }
       case TUNER_LG_NTSC_TAPE:
         {
             TUNERDEF(TUNER_LG_NTSC_TAPE, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0x8e,732);
             break;
         }
       case TUNER_TNF_8831BGFF:
         {
             TUNERDEF(TUNER_TNF_8831BGFF, VIDEOFORMAT_PAL_B,
                 16*(161.25),16*(463.25),0xa0,0x90,0x30,0x8e,623);
             break;
         }
       case TUNER_PHILIPS_FM1256_IH3:
         {
             TUNERDEF(TUNER_PHILIPS_FM1256_IH3, VIDEOFORMAT_PAL_B,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0x8e,623);
             break;
         }
       case TUNER_PHILIPS_FQ1286:
         {
             TUNERDEF(TUNER_PHILIPS_FQ1286, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(454.00),0x41,0x42,0x04,0x8e,940);// UHF band untested
             break;
         }
       case TUNER_LG_PAL_TAPE:
         {
             TUNERDEF(TUNER_LG_PAL_TAPE, VIDEOFORMAT_PAL_B,
                 16*(170.00),16*(450.00),0x01,0x02,0x08,0xce,623);
             break;
         }
       case TUNER_PHILIPS_FQ1216AME_MK4:
         {
             TUNERDEF(TUNER_PHILIPS_FQ1216AME_MK4, VIDEOFORMAT_PAL_B,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0xce,623);
             break;
         }
       case TUNER_PHILIPS_FQ1236A_MK4:
         {
             TUNERDEF(TUNER_PHILIPS_FQ1236A_MK4, VIDEOFORMAT_PAL_B,
                 16*(160.00),16*(442.00),0x01,0x02,0x04,0x8e,732);
             break;
         }
       case TUNER_YMEC_TVF_8531MF:
         {
             TUNERDEF(TUNER_YMEC_TVF_8531MF, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(454.00),0xa0,0x90,0x30,0x8e,732);
             break;
         }
       case TUNER_YMEC_TVF_5533MF:
         {
             TUNERDEF(TUNER_YMEC_TVF_5533MF, VIDEOFORMAT_NTSC_M,
                 16*(160.00),16*(454.00),0x01,0x02,0x04,0x8e,732);
             break;
         }
       case TUNER_TENA_9533_DI:
         {
             TUNERDEF(TUNER_TENA_9533_DI, VIDEOFORMAT_PAL_B,
                 16*(160.25),16*(464.25),0x01,0x02,0x04,0x8e,623);
             break;
         }
       case TUNER_PHILIPS_FMD1216ME_MK3:
         {
             TUNERDEF(TUNER_PHILIPS_FMD1216ME_MK3, VIDEOFORMAT_PAL_B,
                 16*(160.00),16*(442.00),0x51,0x52,0x54,0x86,623);
             break;
         }
    }
}

// ---------------------------------------------------------------------------
// Hauppauge EEPROM tuner identifiers
// - used for bt8x8 and cx8800 cards
//
static const eTunerId m_TunerHauppaugeAnalog[] =
{
    /* 0-9 */
    TUNER_ABSENT,                       //"None"
    TUNER_ABSENT,                       //"External"
    TUNER_ABSENT,                       //"Unspecified"
    TUNER_PHILIPS_PAL,                  //"Philips FI1216"
    TUNER_PHILIPS_SECAM,                //"Philips FI1216MF"
    TUNER_PHILIPS_NTSC,                 //"Philips FI1236"
    TUNER_PHILIPS_PAL_I,                //"Philips FI1246"
    TUNER_PHILIPS_PAL_DK,               //"Philips FI1256"
    TUNER_PHILIPS_PAL,                  //"Philips FI1216 MK2"
    TUNER_PHILIPS_SECAM,                //"Philips FI1216MF MK2"
    /* 10-19 */
    TUNER_PHILIPS_NTSC,                 //"Philips FI1236 MK2"
    TUNER_PHILIPS_PAL_I,                //"Philips FI1246 MK2"
    TUNER_PHILIPS_PAL_DK,               //"Philips FI1256 MK2"
    TUNER_TEMIC_4032FY5_NTSC,           //"Temic 4032FY5"
    TUNER_TEMIC_4002FH5_PAL,            //"Temic 4002FH5"
    TUNER_TEMIC_4062FY5_PAL_I,          //"Temic 4062FY5"
    TUNER_PHILIPS_PAL,                  //"Philips FR1216 MK2"
    TUNER_PHILIPS_SECAM,                //"Philips FR1216MF MK2"
    TUNER_PHILIPS_NTSC,                 //"Philips FR1236 MK2"
    TUNER_PHILIPS_PAL_I,                //"Philips FR1246 MK2"
    /* 20-29 */
    TUNER_PHILIPS_PAL_DK,               //"Philips FR1256 MK2"
    TUNER_PHILIPS_PAL,                  //"Philips FM1216"
    TUNER_PHILIPS_SECAM,                //"Philips FM1216MF"
    TUNER_PHILIPS_NTSC,                 //"Philips FM1236"
    TUNER_PHILIPS_PAL_I,                //"Philips FM1246"
    TUNER_PHILIPS_PAL_DK,               //"Philips FM1256"
    TUNER_TEMIC_4036FY5_NTSC,           //"Temic 4036FY5"
    TUNER_ABSENT,                       //"Samsung TCPN9082D"
    TUNER_ABSENT,                       //"Samsung TCPM9092P"
    TUNER_TEMIC_4006FH5_PAL,            //"Temic 4006FH5"
    /* 30-39 */
    TUNER_ABSENT,                       //"Samsung TCPN9085D"
    TUNER_ABSENT,                       //"Samsung TCPB9085P"
    TUNER_ABSENT,                       //"Samsung TCPL9091P"
    TUNER_TEMIC_4039FR5_NTSC,           //"Temic 4039FR5"
    TUNER_PHILIPS_MULTI,                //"Philips FQ1216 ME"
    TUNER_TEMIC_4066FY5_PAL_I,          //"Temic 4066FY5"
    TUNER_PHILIPS_NTSC,                 //"Philips TD1536"
    TUNER_PHILIPS_NTSC,                 //"Philips TD1536D"
    TUNER_PHILIPS_NTSC,                 //"Philips FMR1236"
    TUNER_ABSENT,                       //"Philips FI1256MP"
    /* 40-49 */
    TUNER_ABSENT,                       //"Samsung TCPQ9091P"
    TUNER_TEMIC_4006FN5_PAL,            //"Temic 4006FN5"
    TUNER_TEMIC_4009FR5_PAL,            //"Temic 4009FR5"
    TUNER_TEMIC_4046FM5_MULTI,          //"Temic 4046FM5"
    TUNER_TEMIC_4009FN5_PAL,            //"Temic 4009FN5"
    TUNER_ABSENT,                       //"Philips TD1536D_FH_44"
    TUNER_LG_R01F_NTSC,                 //"LG TPI8NSR01F"
    TUNER_LG_B01D_PAL,                  //"LG TPI8PSB01D"
    TUNER_LG_B11D_PAL,                  //"LG TPI8PSB11D"
    TUNER_LG_I001D_PAL_I,               //"LG TAPC-I001D"
    /* 50-59 */
    TUNER_LG_I701D_PAL_I,               //"LG TAPC-I701D"
    TUNER_ABSENT,                       //"Temic 4042FI5"
    TUNER_ABSENT,                       //"Microtune 4049 FM5"
    TUNER_ABSENT,                       //"LG TPI8NSR11F"
    TUNER_ABSENT,                       //"Microtune 4049 FM5 Alt I2C"
    TUNER_ABSENT,                       //"Philips FQ1216ME MK3"
    TUNER_PHILIPS_FM1236_MK3,           //"Philips FI1236 MK3"
    TUNER_PHILIPS_FM1216ME_MK3,         //"Philips FM1216 ME MK3"
    TUNER_PHILIPS_FM1236_MK3,           //"Philips FM1236 MK3"
    TUNER_ABSENT,                       //"Philips FM1216MP MK3"
    /* 60-69 */
    TUNER_PHILIPS_FM1216ME_MK3,         //"LG S001D MK3"
    TUNER_ABSENT,                       //"LG M001D MK3"
    TUNER_ABSENT,                       //"LG S701D MK3"
    TUNER_ABSENT,                       //"LG M701D MK3"
    TUNER_ABSENT,                       //"Temic 4146FM5"
    TUNER_TEMIC_4136FY5,                //"Temic 4136FY5"
    TUNER_TEMIC_4106FH5,                //"Temic 4106FH5"
    TUNER_ABSENT,                       //"Philips FQ1216LMP MK3"
    TUNER_LG_NTSC_TAPE,                 //"LG TAPE H001F MK3"
    TUNER_ABSENT,                       //"LG TAPE H701F MK3"
    /* 70-79 */
    TUNER_ABSENT,                       //"LG TALN H200T"
    TUNER_ABSENT,                       //"LG TALN H250T"
    TUNER_ABSENT,                       //"LG TALN M200T"
    TUNER_ABSENT,                       //"LG TALN Z200T"
    TUNER_ABSENT,                       //"LG TALN S200T"
    TUNER_ABSENT,                       //"Thompson DTT7595"
    TUNER_ABSENT,                       //"Thompson DTT7592"
    TUNER_ABSENT,                       //"Silicon TDA8275C1 8290"
    TUNER_ABSENT,                       //"Silicon TDA8275C1 8290 FM"
    TUNER_ABSENT,                       //"Thompson DTT757"
    /* 80-89 */
    TUNER_ABSENT,                       //"Philips FQ1216LME MK3"
    TUNER_ABSENT,                       //"LG TAPC G701D"
    TUNER_LG_TAPCNEW_NTSC,              //"LG TAPC H791F"
    TUNER_LG_TAPCNEW_PAL,               //"TCL 2002MB 3"
    TUNER_LG_TAPCNEW_PAL,               //"TCL 2002MI 3"
    TUNER_TCL_2002N,                    //"TCL 2002N 6A"
    TUNER_ABSENT,                       //"Philips FQ1236 MK3"
    TUNER_ABSENT,                       //"Samsung TCPN 2121P30A"
    TUNER_ABSENT,                       //"Samsung TCPE 4121P30A"
    TUNER_PHILIPS_FM1216ME_MK3,         //"TCL MFPE05 2"
    /* 90-99 */
    TUNER_ABSENT,                       //"LG TALN H202T"
    TUNER_PHILIPS_FQ1216AME_MK4,        //"Philips FQ1216AME MK4"
    TUNER_PHILIPS_FQ1236A_MK4,          //"Philips FQ1236A MK4"
    TUNER_ABSENT,                       //"Philips FQ1286A MK4"
    TUNER_ABSENT,                       //"Philips FQ1216ME MK5"
    TUNER_ABSENT,                       //"Philips FQ1236 MK5"
    TUNER_ABSENT,                       //"Unspecified"
    TUNER_LG_PAL_TAPE,                  //"LG PAL (TAPE Series)"
};

// ---------------------------------------------------------------------------
// Generic I2C interface: pass calls through to hardware-specific driver
//
static void I2CBus_Lock( void )
{
   DsDrv_LockCard();
}

static void I2CBus_Unlock( void )
{
   DsDrv_UnlockCard();
}

static BOOL I2CBus_Write( const BYTE * writeBuffer, size_t writeBufferSize )
{
   BOOL  result;

   if ( (pTvCard != NULL) && (pTvCard->i2cBus != NULL) && (pTvCard->i2cBus->I2cWrite != NULL) )
   {
      result = pTvCard->i2cBus->I2cWrite(pTvCard, writeBuffer, writeBufferSize);

      ifdebug2(!result, "I2CBus-Write: failed for device 0x%02X, %d bytes", writeBuffer[0], writeBufferSize);
   }
   else
   {
      fatal3("I2CBus-Write: no card or I2C bus configured (%lX,%lX,%lX)", (long)pTvCard, (long)((pTvCard != NULL) ? pTvCard->i2cBus : NULL), (long)(((pTvCard != NULL) && (pTvCard->i2cBus != NULL)) ? pTvCard->i2cBus->I2cWrite : NULL));
      result = FALSE;
   }
   return result;
}

// ---------------------------------------------------------------------------
// Import of DScaler TDA8290.cpp support: from class CTDA8290

// The I2C address of the TDA8290 chip.
#define I2C_ADDR_TDA8290        0x4B

// Subaddresses used by TDA8290.  Addresses marked R
// are read mode.  The rest are write mode.
#define TDA8290_CLEAR                       0x00
#define TDA8290_STANDARD_REG                0x01
#define TDA8290_DIV_FUNC                    0x02
#define TDA8290_FILTERS_1                   0x03
#define TDA8290_FILTERS_2                   0x04
#define TDA8290_ADC_HEADR                   0x05
#define TDA8290_GRP_DELAY                   0x06
#define TDA8290_DTO_PC                      0x07
#define TDA8290_PC_PLL_FUNC                 0x08
#define TDA8290_AGC_FUNC                    0x09
#define TDA8290_IF_AGC_SET                  0x0F
#define TDA8290_T_AGC_FUNC                  0x10
#define TDA8290_TOP_ADJUST_REG              0x11
#define TDA8290_SOUNDSET_1                  0x13
#define TDA8290_SOUNDSET_2                  0x14
// ... address defines skipped ...
#define TDA8290_ADC_SAT                     0x1A    // R
#define TDA8290_AFC_REG                     0x1B    // R
#define TDA8290_AGC_HVPLL                   0x1C    // R
#define TDA8290_IF_AGC_STAT                 0x1D    // R
#define TDA8290_T_AGC_STAT                  0x1E    // R
#define TDA8290_IDENTITY                    0x1F    // R
#define TDA8290_GPIO1                       0x20
#define TDA8290_GPIO2                       0x21
#define TDA8290_GPIO3                       0x22
#define TDA8290_TEST                        0x23
#define TDA8290_PLL1                        0x24
#define TDA8290_PLL1R                       0x24    // R
#define TDA8290_PLL2                        0x25
#define TDA8290_PLL3                        0x26
#define TDA8290_PLL4                        0x27
#define TDA8290_ADC                         0x28
// ... address defines skipped ...
#define TDA8290_PLL5                        0x2C
#define TDA8290_PLL6                        0x2D
#define TDA8290_PLL7                        0x2E
#define TDA8290_PLL8                        0x2F    //R
#define TDA8290_V_SYNC_DEL                  0x30

typedef enum
{
    TDA8290_STANDARD_MN = 0,
    TDA8290_STANDARD_B,
    TDA8290_STANDARD_GH,
    TDA8290_STANDARD_I,
    TDA8290_STANDARD_DK,
    TDA8290_STANDARD_L,
    TDA8290_STANDARD_L2,
    TDA8290_STANDARD_RADIO,
    TDA8290_STANDARD_LASTONE
} eTDA8290Standard;

static void Tda8290_SetVideoSystemStandard(eTDA8290Standard standard);
static eTDA8290Standard Tda8290_GetTDA8290Standard(uint norm /*eVideoFormat videoFormat*/);

static BOOL Tda8290_WriteToSubAddress(BYTE subAddress, BYTE writeByte)
{
   BYTE buf[3];

   buf[0] = I2C_ADDR_TDA8290 << 1;
   buf[1] = subAddress;
   buf[2] = writeByte;

   return I2CBus_Write(buf, 3);
}

static BOOL Tda8290_ReadFromSubAddress(BYTE subAddress, BYTE *readBuffer, size_t len)
{
   BYTE addr[2];

   addr[0] = I2C_ADDR_TDA8290 << 1;
   addr[1] = subAddress;

   return pTvCard->i2cBus->I2cRead(pTvCard, addr, sizeof(addr), readBuffer, len);
}

static void Tda8290_Init(bool bPreInit, uint norm /*eVideoFormat videoFormat*/ )
{
    if (bPreInit)
    {
        /* Write the default value into the CLEAR register. Sets soft reset and
           standby to the "normal operation" setting. There's no video or audio
           without this. */
        Tda8290_WriteToSubAddress(TDA8290_CLEAR, 0x1);

        /* DEBUG:
            Sets
            GP2_CF (Bits 0-3) (GPIO2 Pin Configuration) to 1
            IICSW_ON to 1 (I2C Switch Command)
            IICSW_EN to 1 (Enable GPIO_1,2 as I2C switch) */
        Tda8290_WriteToSubAddress(TDA8290_GPIO2, 0xC1);
    }
    else
    {
        // V-Sync: positive pulse, line 15, width 128us (data-sheet initialization)
        Tda8290_WriteToSubAddress(TDA8290_V_SYNC_DEL, 0x6F);
        // Set GP0 to VSYNC.  The data-sheet says this but the v4l2 code
        // doesn't do this for some reason.
        Tda8290_WriteToSubAddress(TDA8290_GPIO1, 0x0B);
        // Set TDA8290 gate to prevent TDA8275 communication
        Tda8290_WriteToSubAddress(TDA8290_GPIO2, 0x81);
    }
}

static void Tda8290_TunerSet(bool bPreSet, uint norm /*eVideoFormat videoFormat*/ )
{
    eTDA8290Standard standard = Tda8290_GetTDA8290Standard(norm /*videoFormat*/);

    if (bPreSet)
    {
        // Set video system standard.
        Tda8290_SetVideoSystemStandard(standard);

        // "1.1 Set input ADC register" (data-sheet)
        Tda8290_WriteToSubAddress(TDA8290_ADC, 0x14);
        // "1.2 Increasing IF AGC speed" (data-sheet)
        // This is the register's default value.
        Tda8290_WriteToSubAddress(TDA8290_IF_AGC_SET, 0x88);
        // "1.3 Set ADC headroom (TDA8290) to nominal" (data-sheet)
        //if (standard == TDA8290_STANDARD_L || standard == TDA8290_STANDARD_L2)
        if (norm == VIDEO_MODE_SECAM)
        {
            // 9dB ADC headroom if standard is L or L'
            Tda8290_WriteToSubAddress(TDA8290_ADC_HEADR, 0x2);
        }
        //else
        //{
        //    // 6dB ADC headroom
        //    Tda8290_WriteToSubAddress(TDA8290_ADC_HEADR, 0x4);
        //}

        // 1.4 Set picture carrier PLL BW (TDA8290) to nominal (data-sheet)
        // This is the register's default value.
        Tda8290_WriteToSubAddress(TDA8290_PC_PLL_FUNC, 0x47);

        // The v4l code does this instead of 1.2, 1.3 and 1.4
        // Tda8290_WriteToSubAddress(TDA8290_CLEAR, 0x00);

        // Set TDA8290 gate for TDA8275 communication
        Tda8290_WriteToSubAddress(TDA8290_GPIO2, 0xC1);
    }
    else
    {
        BYTE readByte = 0x00;

        // 4 Switching AGCF gain to keep 8dB headroom for RF power variations
        if ( ( (Tda8290_ReadFromSubAddress(TDA8290_IF_AGC_STAT, &readByte, 1) && (readByte > 155)) ||
               (Tda8290_ReadFromSubAddress(TDA8290_AFC_REG, &readByte, 1) && ((readByte & 0x80) == 0)) ) &&
             (Tda8290_ReadFromSubAddress(TDA8290_ADC_SAT, &readByte, 1) && (readByte < 20)) )
        {
            // 1Vpp for TDA8290 ADC
            Tda8290_WriteToSubAddress(TDA8290_ADC, 0x54);
            Sleep(100);

            // This cannot be done because there's no interface from here to the TDA8275.
            //if ((Tda8290_ReadFromSubAddress(TDA8290_IF_AGC_STAT, &readByte, 1) && readByte > 155 ||
            //  Tda8290_ReadFromSubAddress(TDA8290_AFC_REG, &readByte, 1) && (readByte & 0x80) == 0))
            //{
            //  // AGCF gain is increased to 10-30dB.
            //  TDA8275->Tda8290_WriteToSubAddress(TDA8275_AB4, 0x08);
            //  Sleep(100);

                if ( (Tda8290_ReadFromSubAddress(TDA8290_IF_AGC_STAT, &readByte, 1) && (readByte > 155)) ||
                     (Tda8290_ReadFromSubAddress(TDA8290_AFC_REG, &readByte, 1) && ((readByte & 0x80) == 0)) )
                {
                    // 12dB ADC headroom
                    Tda8290_WriteToSubAddress(TDA8290_ADC_HEADR, 0x1);
                    // 70kHz PC PLL BW to counteract striping in picture
                    Tda8290_WriteToSubAddress(TDA8290_PC_PLL_FUNC, 0x27);
                    // Wait for IF AGC
                    Sleep(100);
                }
            //}
        }

        // 5/ RESET for L/L' deadlock
        //if (standard == TDA8290_STANDARD_L || standard == TDA8290_STANDARD_L2)
        if (norm == VIDEO_MODE_SECAM)
        {
            if ((Tda8290_ReadFromSubAddress(TDA8290_ADC_SAT, &readByte, 1) && (readByte > 20)) ||
                (Tda8290_ReadFromSubAddress(TDA8290_AFC_REG, &readByte, 1) && ((readByte & 0x80) == 0)) )
            {
                // Reset AGC integrator
                Tda8290_WriteToSubAddress(TDA8290_AGC_FUNC, 0x0B);
                Sleep(40);
                // Normal mode
                Tda8290_WriteToSubAddress(TDA8290_AGC_FUNC, 0x09);
            }
        }

        // 6/ Set TDA8290 gate to prevent TDA8275 communication
        Tda8290_WriteToSubAddress(TDA8290_GPIO2, 0x81);
        // 7/ IF AGC control loop bandwidth
        Tda8290_WriteToSubAddress(TDA8290_IF_AGC_SET, 0x81);
    }
}

static BOOL Tda8290_Detect( void )
{
    BYTE readBuffer;

    // I'm not sure about this.  Maybe the value of identify needs to be tested
    // too, and maybe it's necessary to perform read tests on other registers.
    if (!Tda8290_ReadFromSubAddress(TDA8290_IDENTITY, &readBuffer, 1))
    {
        dprintf0("TDA8290: not detected\n");
        return FALSE;
    }

    dprintf1("Tda8290-Detect: read $1F = %02x\n", readBuffer);
    return TRUE;
}

static void Tda8290_SetVideoSystemStandard(eTDA8290Standard standard)
{
    const BYTE sgStandard[TDA8290_STANDARD_LASTONE] = { 1, 2, 4, 8, 16, 32, 64, 0 };

    if ( (standard == TDA8290_STANDARD_RADIO)
         || (standard > TDA8290_STANDARD_LASTONE) )
    {
        return;
    }

    // Bits: 0..6 := standard, 7 := expert mode
    Tda8290_WriteToSubAddress(TDA8290_STANDARD_REG, sgStandard[(int)standard]);
    // Activate expert mode.  I think it might need to be broken into two
    // calls like this so the first call sets everything up in easy mode
    // before expert mode is switched on.
    Tda8290_WriteToSubAddress(TDA8290_STANDARD_REG, sgStandard[(int)standard]|0x80);
}

static eTDA8290Standard Tda8290_GetTDA8290Standard(uint norm /*eVideoFormat videoFormat*/)
{
    eTDA8290Standard standard = TDA8290_STANDARD_MN;

    switch (norm /*videoFormat*/)
    {
    //case VIDEOFORMAT_PAL_B:
    //case VIDEOFORMAT_SECAM_B:
    case VIDEO_MODE_PAL:
        standard = TDA8290_STANDARD_B;
        break;
    //case VIDEOFORMAT_PAL_G:
    //case VIDEOFORMAT_PAL_H:        
    //case VIDEOFORMAT_PAL_N:
    //case VIDEOFORMAT_SECAM_G:
    //case VIDEOFORMAT_SECAM_H:
        //standard = TDA8290_STANDARD_GH;
        //break;
    //case VIDEOFORMAT_PAL_I:
        //standard = TDA8290_STANDARD_I;
        //break;
    //case VIDEOFORMAT_PAL_D:
    //case VIDEOFORMAT_SECAM_D:   
    //case VIDEOFORMAT_SECAM_K:
    //case VIDEOFORMAT_SECAM_K1:
        //standard = TDA8290_STANDARD_DK;
        //break;
    //case VIDEOFORMAT_SECAM_L:
    //case VIDEOFORMAT_SECAM_L1:
    case VIDEO_MODE_SECAM:
        standard = TDA8290_STANDARD_L;
        break;
    //case VIDEOFORMAT_PAL_60:    
        // Unsupported
        //break;
    //case VIDEOFORMAT_PAL_M:
    //case VIDEOFORMAT_PAL_N_COMBO:
    //case VIDEOFORMAT_NTSC_M:
    case VIDEO_MODE_NTSC:
        standard = TDA8290_STANDARD_MN;
        break;
    //case VIDEOFORMAT_NTSC_50:
    //case VIDEOFORMAT_NTSC_M_Japan:
        //standard = TDA8290_STANDARD_MN;
        //break;
    // This value is used among ITuner and IExternalIFDemodulator for radio.
    //case (VIDEOFORMAT_LASTONE+1):
        //standard = TDA8290_STANDARD_RADIO;
        //break;
    }

    return standard;
}


// ---------------------------------------------------------------------------
// @file TDA8290.cpp CTDA8290 Implementation
//
// The I2C addresses for the TDA8275 chip.
// (These are standard tuner addresses.  a.k.a C0, C2, C4, C6)
#define I2C_ADDR_TDA8275_0      0x60
#define I2C_ADDR_TDA8275_1      0x61
#define I2C_ADDR_TDA8275_2      0x62
#define I2C_ADDR_TDA8275_3      0x63

// Common Subaddresses used by TDA8275 and TDA8275A (Read-Only status register)
#define TDA8275_SR0             0x00
#define TDA8275_SR1             0x10
#define TDA8275_SR2             0x20
#define TDA8275_SR3             0x30

// Common Subaddresses used by TDA8275 and TDA8275A.
#define TDA8275_DB1             0x00
#define TDA8275_DB2             0x10
#define TDA8275_DB3             0x20
#define TDA8275_CB1             0x30
#define TDA8275_BB              0x40
#define TDA8275_AB1             0x50
#define TDA8275_AB2             0x60

// Subaddresses used by TDA8275 only.
#define TDA8275_AB3             0x70
#define TDA8275_AB4             0x80
#define TDA8275_GB              0x90
#define TDA8275_TB              0xA0
#define TDA8275_SDB3            0xB0
#define TDA8275_SDB4            0xC0

// Subaddresses used by TDA8275A only.
#define TDA8275A_IB1            0x70
#define TDA8275A_AB3            0x80
#define TDA8275A_IB2            0x90
#define TDA8275A_CB2            0xA0
#define TDA8275A_IB3            0xB0
#define TDA8275A_CB3            0xC0

typedef struct
{
    WORD    loMin;
    WORD    loMax;
    BYTE    spd;
    BYTE    BS;
    BYTE    BP;
    BYTE    CP;
    BYTE    GC3;
    BYTE    div1p5;
} tProgramingParam;

typedef struct
{
    WORD    loMin;
    WORD    loMax;
    BYTE    SVCO;
    BYTE    SPD;
    BYTE    SCR;
    BYTE    SBS;
    BYTE    GC3;

} tProgramingParam2;

typedef struct
{
    WORD    loMin;
    WORD    loMax;
    BYTE    SVCO;
    BYTE    SPD;
    BYTE    SCR;
    BYTE    SBS;
    BYTE    GC3;

} tProgramingParam3;

typedef struct
{
    WORD    sgIFkHz;
    BYTE    sgIFLPFilter;
} tStandardParam;

// TDA8275 analog TV frequency dependent parameters (prgTab)
static const tProgramingParam Tda8275_k_programmingTable[] =
{
    // LOmin,   LOmax,  spd,    BS,     BP,     CP,     GC3,    div1p5
    { 55,       62,     3,      2,      0,      0,      3,      1   },
    { 62,       66,     3,      3,      0,      0,      3,      1   },
    { 66,       76,     3,      1,      0,      0,      3,      0   },
    { 76,       84,     3,      2,      0,      0,      3,      0   },
    { 84,       93,     3,      2,      0,      0,      1,      0   },
    { 93,       98,     3,      3,      0,      0,      1,      0   },
    { 98,       109,    3,      3,      1,      0,      1,      0   },
    { 109,      123,    2,      2,      1,      0,      1,      1   },
    { 123,      133,    2,      3,      1,      0,      1,      1   },
    { 133,      151,    2,      1,      1,      0,      1,      0   },
    { 151,      154,    2,      2,      1,      0,      1,      0   },
    { 154,      181,    2,      2,      1,      0,      0,      0   },
    { 181,      185,    2,      2,      2,      0,      1,      0   },
    { 185,      217,    2,      3,      2,      0,      1,      0   },
    { 217,      244,    1,      2,      2,      0,      1,      1   },
    { 244,      265,    1,      3,      2,      0,      1,      1   },
    { 265,      302,    1,      1,      2,      0,      1,      0   },
    { 302,      324,    1,      2,      2,      0,      1,      0   },
    { 324,      370,    1,      2,      3,      0,      1,      0   },
    { 370,      454,    1,      3,      3,      0,      1,      0   },
    { 454,      493,    0,      2,      3,      0,      1,      1   },
    { 493,      530,    0,      3,      3,      0,      1,      1   },
    { 530,      554,    0,      1,      3,      0,      1,      0   },
    { 554,      604,    0,      1,      4,      0,      0,      0   },
    { 604,      696,    0,      2,      4,      0,      0,      0   },
    { 696,      740,    0,      2,      4,      1,      0,      0   },
    { 740,      820,    0,      3,      4,      0,      0,      0   },
    { 820,      865,    0,      3,      4,      1,      0,      0   }
};

// TDA8275A analog TV frequency dependent parameters (prgTab)
static const tProgramingParam2 Tda8275_k_programmingTable2[] =
{
    // loMin,   loMax,  SVCO,   SPD,    SCR,    SBS,    GC3
    { 49,       56,     3,      4,      0,      0,      3   },
    { 56,       67,     0,      3,      0,      0,      3   },
    { 67,       81,     1,      3,      0,      0,      3   },
    { 81,       97,     2,      3,      0,      0,      3   },
    { 97,       113,    3,      3,      0,      1,      1   },
    { 113,      134,    0,      2,      0,      1,      1   },
    { 134,      154,    1,      2,      0,      1,      1   },
    { 154,      162,    1,      2,      0,      1,      1   },
    { 162,      183,    2,      2,      0,      1,      1   },
    { 183,      195,    2,      2,      0,      2,      1   },
    { 195,      227,    3,      2,      0,      2,      3   },
    { 227,      269,    0,      1,      0,      2,      3   },
    { 269,      325,    1,      1,      0,      2,      1   },
    { 325,      390,    2,      1,      0,      3,      3   },
    { 390,      455,    3,      1,      0,      3,      3   },
    { 455,      520,    0,      0,      0,      3,      1   },
    { 520,      538,    0,      0,      1,      3,      1   },
    { 538,      554,    1,      0,      0,      3,      1   },
    { 554,      620,    1,      0,      0,      4,      0   },
    { 620,      650,    1,      0,      1,      4,      0   },
    { 650,      700,    2,      0,      0,      4,      0   },
    { 700,      780,    2,      0,      1,      4,      0   },
    { 780,      820,    3,      0,      0,      4,      0   },
    { 820,      870,    3,      0,      1,      4,      0   },
    { 870,      911,    3,      0,      2,      4,      0   }
};

#if 0
// TDA8275A DVB-T frequency dependent parameters (prgTab)
static const tProgramingParam3 Tda8275_k_programmingTable3[] =
{
    // loMin,   loMax,  SVCO,   SPD,    SCR,    SBS,    GC3
    { 49,       57,     3,      4,      0,      0,      1   },
    { 57,       67,     0,      3,      0,      0,      1   },
    { 67,       81,     1,      3,      0,      0,      1   },
    { 81,       98,     2,      3,      0,      0,      1   },
    { 98,       114,    3,      3,      0,      1,      1   },
    { 114,      135,    0,      2,      0,      1,      1   },
    { 135,      154,    1,      2,      0,      1,      1   },
    { 154,      163,    1,      2,      0,      1,      1   },
    { 163,      183,    2,      2,      0,      1,      1   },
    { 183,      195,    2,      2,      0,      2,      1   },
    { 195,      228,    3,      2,      0,      2,      1   },
    { 228,      269,    0,      1,      0,      2,      1   },
    { 269,      290,    1,      1,      0,      2,      1   },
    { 290,      325,    1,      1,      0,      3,      1   },
    { 325,      390,    2,      1,      0,      3,      1   },
    { 390,      455,    3,      1,      0,      3,      1   },
    { 455,      520,    0,      0,      0,      3,      1   },
    { 520,      538,    0,      0,      1,      3,      1   },
    { 538,      550,    1,      0,      0,      3,      1   },
    { 550,      620,    1,      0,      0,      4,      0   },
    { 620,      650,    1,      0,      1,      4,      0   },
    { 650,      700,    2,      0,      0,      4,      0   },
    { 700,      780,    2,      0,      1,      4,      0   },
    { 780,      820,    3,      0,      0,      4,      0   },
    { 820,      870,    3,      0,      1,      4,      0   }
};
#endif

static const tStandardParam Tda8275_k_standardParamTable[TDA8290_STANDARD_LASTONE] =
{
    // sgIFkHz, sgIFLPFilter
    { 5750, 1 },
    { 6750, 0 },
    { 7750, 0 },
    { 7750, 0 },
    { 7750, 0 },
    { 7750, 0 },
    { 1250, 0 },
    { 4750, 0 },  // FM Radio (this value is a guess)
};

static BOOL Tda8275_WriteToSubAddress(BYTE subAddress, BYTE writeByte)
{
   BYTE buf[3];

   buf[0] = TunerDeviceI2C;
   buf[1] = subAddress;
   buf[2] = writeByte;

   return I2CBus_Write(buf, 3);
}


static bool Tda8275_IsTDA8275A( void )
{
    BYTE addr[2] = {TunerDeviceI2C, TDA8275_SR1};
    BYTE Result;

    // Read HID Bit's (18:21) : 0000 = TDA8275
    //                          0010 = TDA8275A

    if (pTvCard->i2cBus->I2cRead(pTvCard, addr, sizeof(addr), &Result, 1))
    {
        if ((Result & 0x3C) == 0x08)
        {
            dprintf0("TDA8275: TDA8275A revision found\n");
            return TRUE;
        }
        else
        {
            dprintf0("TDA8275: Found\n");
            return FALSE;
        }
    }

    debug0("TDA8275: Error while detecting chip revision\n");
    return FALSE;
}

static void Tda8275_WriteTDA8275Initialization( void )
{
    // 2 TDA8275A Initialization
    if (Tda8275_IsTDA8275A())
    {
        Tda8275_WriteToSubAddress(TDA8275_DB1, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_DB2, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_DB3, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_AB1, 0xAB);
        Tda8275_WriteToSubAddress(TDA8275_AB2, 0x3C);
        Tda8275_WriteToSubAddress(TDA8275A_IB1, 0x04);
        Tda8275_WriteToSubAddress(TDA8275A_AB3, 0x24);
        Tda8275_WriteToSubAddress(TDA8275A_IB2, 0xFF);
        Tda8275_WriteToSubAddress(TDA8275A_CB2, 0x40);
        Tda8275_WriteToSubAddress(TDA8275A_IB3, 0x00);
        Tda8275_WriteToSubAddress(TDA8275A_CB3, 0x3B);

        /*
        // These values come from the "2/ TDA8275A Initialization"
        // code in the data-sheet.  Those that weren't specified
        // there were substituted with default values from the
        // default column of the data-sheet.

        BYTE initializationBytes[13] = {
            // DB1,  DB2,  DB3,  CB1
               0x00, 0x00, 0x00, 0xDC,
            // BB,   AB1,  AB2,  IB1
               0x05, 0xAB, 0x3C, 0x04,
            // AB3,  IB2,  CB2,  IB3,  CB3
               0x24, 0xFF, 0x40, 0x00, 0x3B };

        Tda8275_WriteToSubAddress(TDA8275A_DB1, initializationBytes, 13);
        */
    }
    else
    {
        Tda8275_WriteToSubAddress(TDA8275_DB1, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_DB2, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_DB3, 0x40);
        Tda8275_WriteToSubAddress(TDA8275_AB3, 0x2A);
        Tda8275_WriteToSubAddress(TDA8275_GB, 0xFF);
        Tda8275_WriteToSubAddress(TDA8275_TB, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_SDB3, 0x00);
        Tda8275_WriteToSubAddress(TDA8275_SDB4, 0x40);

        /*
        // These values come from the "2/ TDA827x Initialization"
        // code in the data-sheet.  Those that weren't specified
        // there were substituted with default values from the
        // default column of the data-sheet.

        BYTE initializationBytes[13] = {
            // DB1,  DB2,  DB3,  CB1
               0x00, 0x00, 0x40, 0x70,
            // BB,   AB1,  AB2,  AB3
               0x00, 0x83, 0x3F, 0x2A,
            // AB4,  GB,   TB,   SDB3, SDB4
               0x04, 0xFF, 0x00, 0x00, 0x40 };

        Tda8275_WriteToSubAddress(TDA8275_DB1, initializationBytes, 13);
        */
    }
}

static bool Tda8275_SetFrequency(long frequencyHz, eTDA8290Standard standard)
{
    BYTE sgIFLPFilter = Tda8275_k_standardParamTable[(int)standard].sgIFLPFilter;
    LONG sgIFHz = Tda8275_k_standardParamTable[(int)standard].sgIFkHz * 1000;

    // sgRFHz + sgIFHz
    LONG freqRFIFHz = frequencyHz + sgIFHz;

    // The data-sheet says:
    // N11toN0=round((2^spd)*Flo*1000000*(16MHz/(2^6))
    //
    // Then uses this code to get the n11ton0 value:
    // lgN11toN0 = Round((2 ^ prgTab(c, 3)) * (sgRFMHz + sgIFMHz) * 1000000 / (16000000 / 2 ^ 6))
    //
    // Notice the discrepancy with division of (16MHz/2^6).  'prgTab(c, 3)' is row->spd,
    // (sgRFMHz + sgIFMHz) is (freqRFIFHz / 1000000).

    // 0.5 is added for rounding.

    bool success = TRUE;

    if (Tda8275_IsTDA8275A())
    {
#if 0
        if (IsDvbMode())
        {
            // For TDA8275A in DVB Mode
            const tProgramingParam3* row = Tda8275_k_programmingTable3;
            const tProgramingParam3* last = (const tProgramingParam3*)((size_t)row + sizeof(Tda8275_k_programmingTable3)) - 1;
            for ( ; row != last && freqRFIFHz > row->loMax * 1000000; row++) ;
            WORD n11ton0 = (WORD)((double)(1 << row->SPD) * ((double)freqRFIFHz / 250000) + 0.5);

            BYTE channelBytes[12];
            channelBytes[0]  = (n11ton0 >> 6) & 0x3F;
            channelBytes[1]  = (n11ton0 << 2) & 0xFC;
            channelBytes[2]  = 0x00;
            channelBytes[3]  = 0x16;
            channelBytes[4]  = (row->SPD << 5) | (row->SVCO << 3) | (row->SBS);
            channelBytes[5]  = 0x01 << 6 | (row->GC3 << 4) | 0x0B;
            channelBytes[6]  = 0x0C;
            channelBytes[7]  = 0x06;
            channelBytes[8]  = 0x24;
            channelBytes[9]  = 0xFF;
            channelBytes[10] = 0x60;
            channelBytes[11] = 0x00;
            channelBytes[12] = sgIFLPFilter ? 0x3B : 0x39; // 7MHz (US) / 9Mhz (Europe);

            if (!Tda8275_WriteToSubAddress(TDA8275_DB1, channelBytes, 12))
            {
                return false;
            }

            // 2.2 Re-initialize PLL and gain path
            Tda8275_WriteToSubAddress(Tda8275_AB2, 0x3C);
            Tda8275_WriteToSubAddress(TDA8275A_CB2, 0x40);
            Sleep(2);
            Tda8275_WriteToSubAddress(TDA8275_CB1, (0x04 << 2) | (row->SCR >> 2));
            Sleep(550); // 550ms delay required.
            Tda8275_WriteToSubAddress(TDA8275_AB1, (0x02 << 6) | (row->GC3 << 4) | 0x0F);
        }
        else
#endif
        {
            // For TDA8275A in analog TV Mode
            const tProgramingParam2* row = Tda8275_k_programmingTable2;
            const tProgramingParam2* last = (const tProgramingParam2*)((size_t)row + sizeof(Tda8275_k_programmingTable2)) - 1;
            WORD n11ton0;
            BYTE channelBytes[2+12];
            
            // Find the matching row of the programming table for this frequency.
            for ( ; (row != last) && (freqRFIFHz > row->loMax * 1000000); row++)
               ;
            
            n11ton0 = (WORD)((double)(1 << row->SPD) * ((double)freqRFIFHz / 250000) + 0.5);

            channelBytes[0] = TunerDeviceI2C;
            channelBytes[1] = TDA8275_DB1;
            channelBytes[2+0]  = (n11ton0 >> 6) & 0x3F;
            channelBytes[2+1]  = (n11ton0 << 2) & 0xFC;
            channelBytes[2+2]  = 0x00;
            channelBytes[2+3]  = 0x16;
            channelBytes[2+4]  = (row->SPD << 5) | (row->SVCO << 3) | (row->SBS);
            channelBytes[2+5]  = 0x02 << 6 | (row->GC3 << 4) | 0x0B;
            channelBytes[2+6]  = 0x0C;
            channelBytes[2+7]  = 0x04;
            channelBytes[2+8]  = 0x20;
            channelBytes[2+9]  = 0xFF;
            channelBytes[2+10] = 0xE0;
            channelBytes[2+11] = 0x00;
            channelBytes[2+12] = sgIFLPFilter ? 0x3B : 0x39; // 7MHz (US) / 9Mhz (Europe);

            if (!I2CBus_Write(channelBytes, 2+12))
            {
                return FALSE;
            }

            // 2.2 Re-initialize PLL and gain path
            Tda8275_WriteToSubAddress(TDA8275_AB2,  0x3C);
            Tda8275_WriteToSubAddress(TDA8275A_CB2, 0xC0);
            Sleep(2);
            Tda8275_WriteToSubAddress(TDA8275_CB1, (0x04 << 2) | (row->SCR >> 2));
            Sleep(550); // 550ms delay required.
            Tda8275_WriteToSubAddress(TDA8275_AB1, (0x02 << 6) | (row->GC3 << 4) | 0x0F);
            // 3 Enabling VSYNC only for analog TV 
            Tda8275_WriteToSubAddress(TDA8275A_AB3, 0x28);
            Tda8275_WriteToSubAddress(TDA8275A_IB3, 0x01);
            Tda8275_WriteToSubAddress(TDA8275A_CB3, sgIFLPFilter ? 0x3B : 0x39); // 7MHz (US) / 9Mhz (Europe);
        }
    }
    else
    {
        // For TDA8275
        const tProgramingParam* row = Tda8275_k_programmingTable;
        const tProgramingParam* last = (const tProgramingParam*)((size_t)row + sizeof(Tda8275_k_programmingTable)) - 1;
        WORD n11ton0;
        BYTE channelBytes[2+7];
        
        // Find the matching row of the programming table for this frequency.
        for ( ; row != last && freqRFIFHz > row->loMax * 1000000; row++) ;

        n11ton0 = (WORD)((double)(1 << row->spd) * ((double)freqRFIFHz / 250000) + 0.5);

        channelBytes[0] = TunerDeviceI2C;
        channelBytes[1] = TDA8275_DB1;
        channelBytes[2+0] = (n11ton0 >> 6) & 0x3F;
        channelBytes[2+1] = (n11ton0 << 2) & 0xFC;
        channelBytes[2+2] = 0x40;
        channelBytes[2+3] = sgIFLPFilter ? 0x72 : 0x52; // 7MHz (US) / 9Mhz (Europe)
        channelBytes[2+4] = (row->spd << 6)|(row->div1p5 << 5)|(row->BS << 3)|row->BP;
        channelBytes[2+5] = 0x8F | (row->GC3 << 4);
        channelBytes[2+6] = 0x8F;

        if (!I2CBus_Write(channelBytes, 2+7) ||
            !Tda8275_WriteToSubAddress(TDA8275_AB4, 0x00))
        {
            return FALSE;
        }


        // 2.2 Re-initialize PLL and gain path
        success &= Tda8275_WriteToSubAddress(TDA8275_AB2, 0xBF);
        // This puts a delay that may not be necessary.
        //  success &= Tda8275_WriteToSubAddress(TDA8275_CB1, 0xD2);
        //  Sleep(1);
        //  success &= Tda8275_WriteToSubAddress(TDA8275_CB1, 0x56);
        //  Sleep(1); // Only 550us required.
        //  success &= Tda8275_WriteToSubAddress(TDA8275_CB1, 0x52);
        //  Sleep(550); // 550ms delay required.
        success &= Tda8275_WriteToSubAddress(TDA8275_CB1, 0x50|row->CP);

        // 3 Enabling VSYNC mode for AGC2
        Tda8275_WriteToSubAddress(TDA8275_AB2, 0x7F);
        Tda8275_WriteToSubAddress(TDA8275_AB4, 0x08);
    }
    
    return success;
}

static bool Tda8275_SetTVFrequency(long frequencyHz, uint norm /*eVideoFormat videoFormat*/)
{
    bool success;

    if (haveTda8290)
    {
       Tda8290_TunerSet(TRUE, norm /*videoFormat*/);
    }

    success = Tda8275_SetFrequency(frequencyHz, Tda8290_GetTDA8290Standard(norm /*videoFormat*/));

    if (haveTda8290)
    {
       Tda8290_TunerSet(FALSE, norm /*videoFormat*/);
    }

    return success;
}

static bool Tda8275_InitializeTuner( void )
{
    Tda8275_WriteTDA8275Initialization();

    if (haveTda8290)
    {
        Tda8290_Init(FALSE, VIDEO_MODE_PAL);
    }

    return TRUE;
}

// ---------------------------------------------------------------------------
// Autodetect TEA5767
//
static BOOL IsTEA5767PresentAtC0( TVCARD * pTvCard )
{
    BYTE buffer[7] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    BYTE addr = 0xC0;  //I2C_TEA5767

    // Sub addresses are not supported so read all five bytes.
    if (pTvCard->i2cBus->I2cRead(pTvCard, &addr, sizeof(addr), buffer, sizeof(buffer)))
    {
        dprintf0("TEA5767: No I2C device at 0xC0."); 
        return FALSE;
    }

    // If all bytes are the same then it's a tuner and not a tea5767 chip.
    if (buffer[0] == buffer[1] &&
        buffer[0] == buffer[2] &&
        buffer[0] == buffer[3] &&
        buffer[0] == buffer[4])
    {
        dprintf0("TEA5767: Not Found. All bytes are equal.");
        return FALSE;
    }

    // Status bytes:
    // Byte 4: bit 3:1 : CI (Chip Identification) == 0
    //         bit 0   : internally set to 0
    // Byte 5: bit 7:0 : == 0
    if (((buffer[3] & 0x0f) != 0x00) || (buffer[4] != 0x00))
    {
        dprintf0("TEA5767: Not Found. Chip ID is not zero.");
        return FALSE;
    }

    // It seems that tea5767 returns 0xff after the 5th byte
    if ((buffer[5] != 0xff) || (buffer[6] != 0xff))
    {
        dprintf0("TEA5767: Not Found. Returned more than 5 bytes.");
        return FALSE;
    }

    dprintf5("TEA5767: Found. 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X",
             buffer[0], buffer[1], buffer[2], buffer[3], buffer[4]);
    
    return TRUE;
}

// ---------------------------------------------------------------------------
// Support for TDA9887 "IF demodulator"
//
// Explanation about functions of MT2032/MT2050 and TDA9887:
//
//   From:   "MIDIMaker" <midimaker@yandex.ru>
//   Date:   Sat, 20 Mar 2004 23:40:26 +0400
//
//   MT2050 does only High Frequencies to first intermediate frequency (38,9 MHz
//   for PAL and 45,75 MHz for NTSC) conversion. All other work done by TDA9887
//   wich is multistandard IF demodulatior. It can be programmed for all
//   standards used worldwide so it will produce composite signal, intermediate
//   sound carrier and demodulated mono sound.
//
// The following code for TDA9887 was taken verbatim from DScaler,
// however stripped down to support PAL/SECAM only:
//

#define I2C_TDA9887_x         0    // placehoder only
#define I2C_TDA9887_0      0x86    // MAD1
#define I2C_TDA9887_1      0x96    // MAD3
#define I2C_TDA9887_2      0x84    // MAD2
#define I2C_TDA9887_3      0x94    // MAD4


static const BYTE tda9887set_pal_bg[] =   {0, 0x00, 0x96, 0x70, 0x49};
//static const BYTE tda9887set_pal_i[] =    {0, 0x00, 0x96, 0x70, 0x4a};
//static const BYTE tda9887set_pal_dk[] =   {0, 0x00, 0x96, 0x70, 0x4b};
static const BYTE tda9887set_pal_l[] =    {0, 0x00, 0x86, 0x50, 0x4b};
static const BYTE tda9887set_ntsc[] =     {0, 0x00, 0x96, 0x70, 0x44};
//static const BYTE tda9887set_ntsc_jp[] =  {0, 0x00, 0x96, 0x70, 0x40};
//static const BYTE tda9887set_fm_radio[] = {0, 0x00, 0x8e, 0x0d, 0x77};

static void Tda9887_TunerSet( bool bPreSet, uint norm /* eVideoFormat videoFormat */ )
{
   BYTE tda9887set[5];

   dprintf2("Tda9887-TunerSet: preset=%d, norm=%d", bPreSet, norm);
   if (bPreSet)
   {
      {
         switch (norm)
         {
            case VIDEO_MODE_PAL:
               memcpy(tda9887set, tda9887set_pal_bg, 5);
               break;

            case VIDEO_MODE_SECAM:
               memcpy(tda9887set, tda9887set_pal_l, 5);
               break;

            case VIDEO_MODE_NTSC:
               memcpy(tda9887set, tda9887set_ntsc, 5);
               break;

            default:
               debug1("TDA9887: Invalid video format %d", norm);
               return;
         }
         dprintf3("Tda9887-TunerSet: 0x%02x 0x%02x 0x%02x", tda9887set[2], tda9887set[3], tda9887set[4]);

         tda9887set[0] = Tda9887DeviceI2C;
         tda9887set[1] = 0;
         I2CBus_Write(tda9887set, 5);
      }
   }
}

static void Tda9887_Init( bool bPreInit, uint norm /* eVideoFormat videoFormat */ )
{
   Tda9887_TunerSet(bPreInit, norm);
}

static bool Tda9887_Detect( BYTE Addr )
{
   static const BYTE tda9887detect[] = {I2C_TDA9887_x, 0x00, 0x54, 0x70, 0x44};
   BYTE tda9887set[5];
   bool result;

   dprintf1("Tda9887-Detect: query I2C bus @%02X...", Addr);
   Tda9887DeviceI2C = Addr;

   memcpy(tda9887set, tda9887detect, sizeof(tda9887set));
   tda9887set[0] = Tda9887DeviceI2C;

   result = I2CBus_Write(tda9887set, 5);

   dprintf1("Tda9887-Detect: ...result: %d", result);
   return result;
}

// ---------------------------------------------------------------------------
// TDA9887 for Pinnacle cards
//
// TDA defines
//

//// first reg
#define TDA9887_VideoTrapBypassOFF      0x00    // bit b0
#define TDA9887_VideoTrapBypassON       0x01    // bit b0

#define TDA9887_AutoMuteFmInactive      0x00    // bit b1
#define TDA9887_AutoMuteFmActive        0x02    // bit b1

#define TDA9887_Intercarrier            0x00    // bit b2
#define TDA9887_QSS                     0x04    // bit b2

#define TDA9887_PositiveAmTV            0x00    // bit b3:4
#define TDA9887_FmRadio                 0x08    // bit b3:4
#define TDA9887_NegativeFmTV            0x10    // bit b3:4

#define TDA9887_ForcedMuteAudioON       0x20    // bit b5
#define TDA9887_ForcedMuteAudioOFF      0x00    // bit b5

#define TDA9887_OutputPort1Active       0x00    // bit b6
#define TDA9887_OutputPort1Inactive     0x40    // bit b6
#define TDA9887_OutputPort2Active       0x00    // bit b7
#define TDA9887_OutputPort2Inactive     0x80    // bit b7

//// second reg
#define TDA9887_TakeOverPointMin        0x00    // bit c0:4
#define TDA9887_TakeOverPointDefault    0x10    // bit c0:4
#define TDA9887_TakeOverPointMax        0x1f    // bit c0:4
#define TDA9887_DeemphasisOFF           0x00    // bit c5
#define TDA9887_DeemphasisON            0x20    // bit c5
#define TDA9887_Deemphasis75            0x00    // bit c6
#define TDA9887_Deemphasis50            0x40    // bit c6
#define TDA9887_AudioGain0              0x00    // bit c7
#define TDA9887_AudioGain6              0x80    // bit c7

//// third reg
#define TDA9887_AudioIF_4_5             0x00    // bit e0:1
#define TDA9887_AudioIF_5_5             0x01    // bit e0:1
#define TDA9887_AudioIF_6_0             0x02    // bit e0:1
#define TDA9887_AudioIF_6_5             0x03    // bit e0:1

#define TDA9887_VideoIF_58_75           0x00    // bit e2:4
#define TDA9887_VideoIF_45_75           0x04    // bit e2:4
#define TDA9887_VideoIF_38_90           0x08    // bit e2:4
#define TDA9887_VideoIF_38_00           0x0C    // bit e2:4
#define TDA9887_VideoIF_33_90           0x10    // bit e2:4
#define TDA9887_VideoIF_33_40           0x14    // bit e2:4
#define TDA9887_RadioIF_45_75           0x18    // bit e2:4
#define TDA9887_RadioIF_38_90           0x1C    // bit e2:4

#define TDA9887_TunerGainNormal         0x00    // bit e5
#define TDA9887_TunerGainLow            0x20    // bit e5

#define TDA9887_Gating_18               0x00    // bit e6
#define TDA9887_Gating_36               0x40    // bit e6

#define TDA9887_AgcOutON                0x80    // bit e7
#define TDA9887_AgcOutOFF               0x00    // bit e7



static void TDA9887Pinnacle_TunerSet(bool bPreSet, uint norm /* eVideoFormat videoFormat */)
{
    static BYTE bData[5];

    dprintf3("TDA9887Pinnacle-TunerSet: preset=%d, norm=%d (last %d)", bPreSet, norm, m_LastVideoFormat);
    if (norm != m_LastVideoFormat)
    {
        BYTE   bVideoIF     = 0;
        BYTE   bAudioIF     = 0;
        BYTE   bDeEmphasis  = 0;
        BYTE   bDeEmphVal   = 0;
        BYTE   bModulation  = 0;
        BYTE   bCarrierMode = 0;
        BYTE   bOutPort1    = TDA9887_OutputPort1Inactive;
        BYTE   bOutPort2    = 0; //TDA9887_OutputPort2Inactive
        BYTE   bVideoTrap   = TDA9887_VideoTrapBypassOFF;
        BYTE   bTopAdjust   = 0x0e; //mbAGC;

        m_LastVideoFormat = norm;

        if (norm == VIDEO_MODE_PAL)
        {
            bDeEmphasis  = TDA9887_DeemphasisON;
            bDeEmphVal   = TDA9887_Deemphasis50;
            bModulation  = TDA9887_NegativeFmTV;
            if (isTda9887PinnacleMono)  // TVCARD_MIRO
            {
                bCarrierMode = TDA9887_Intercarrier;
            }
            else  // stereo boards
            {
                bCarrierMode = TDA9887_QSS;
            }
            bOutPort1    = TDA9887_OutputPort1Inactive;
        }
        else if (norm == VIDEO_MODE_SECAM)
        {
            bDeEmphasis  = TDA9887_DeemphasisON;
            bDeEmphVal   = TDA9887_Deemphasis50;
            bModulation  = TDA9887_NegativeFmTV;
            bCarrierMode = TDA9887_QSS;
            bOutPort1    = TDA9887_OutputPort1Inactive;

            //if ((videoFormat == VIDEOFORMAT_SECAM_L) || (videoFormat == VIDEOFORMAT_SECAM_L1))
            {
                bDeEmphasis  = TDA9887_DeemphasisOFF;
                bDeEmphVal   = TDA9887_Deemphasis75;
                bModulation  = TDA9887_PositiveAmTV;
            }
        }
        else
            debug1("Pinnacte-TunerSet: unsupported norm %d", norm);

        //Set bandwidths

        bVideoIF     = TDA9887_VideoIF_38_90;
        bAudioIF     = TDA9887_AudioIF_5_5;
        switch (norm)
        {
        case VIDEO_MODE_PAL:
        //case VIDEOFORMAT_PAL_B:
        //case VIDEOFORMAT_PAL_G:
        //case VIDEOFORMAT_PAL_H:
            bVideoIF     = TDA9887_VideoIF_38_90;
            bAudioIF     = TDA9887_AudioIF_5_5;
            break;
        //case VIDEOFORMAT_PAL_D:
            //bVideoIF     = TDA9887_VideoIF_38_00;
            //bAudioIF     = TDA9887_AudioIF_6_5;
            //break;
        //case VIDEOFORMAT_PAL_I:
            //bVideoIF     = TDA9887_VideoIF_38_90;
            //bAudioIF     = TDA9887_AudioIF_6_0;
            //break;
        //case VIDEOFORMAT_PAL_M:
        //case VIDEOFORMAT_PAL_N:
        //case VIDEOFORMAT_PAL_N_COMBO:   //Not sure about PAL-NC
            //bVideoIF     = TDA9887_VideoIF_45_75;
            //bAudioIF     = TDA9887_AudioIF_4_5;

        case VIDEO_MODE_SECAM:
        //case VIDEOFORMAT_SECAM_D:
            //bVideoIF     = TDA9887_VideoIF_38_00;
            //bAudioIF     = TDA9887_AudioIF_6_5;
            //break;
        //case VIDEOFORMAT_SECAM_K:
        //case VIDEOFORMAT_SECAM_K1:
            //bVideoIF     = TDA9887_VideoIF_38_90;
            //bAudioIF     = TDA9887_AudioIF_6_5;
            //break;
        //case VIDEOFORMAT_SECAM_L:
            bVideoIF     = TDA9887_VideoIF_38_90;
            bAudioIF     = TDA9887_AudioIF_6_5;
            break;
        //case VIDEOFORMAT_SECAM_L1:
            //bVideoIF     = TDA9887_VideoIF_33_90;
            //bAudioIF     = TDA9887_AudioIF_6_5;
            //break;
        }

        bData[0] =  Tda9887DeviceI2C;
        bData[1] =  0;

        bData[2] =  bVideoTrap                   |  // B0: video trap bypass
                    TDA9887_AutoMuteFmInactive   |  // B1: auto mute
                    bCarrierMode                 |  // B2: InterCarrier for PAL else QSS
                    bModulation                  |  // B3 - B4: positive AM TV for SECAM only
                    TDA9887_ForcedMuteAudioOFF   |  // B5: forced Audio Mute (off)
                    bOutPort1                    |  // B6: Out Port 1
                    bOutPort2;                      // B7: Out Port 2
        bData[3] =  bTopAdjust                   |  // C0 - C4: Top Adjust 0 == -16dB  31 == 15dB
                    bDeEmphasis                  |  // C5: De-emphasis on/off
                    bDeEmphVal                   |  // C6: De-emphasis 50/75 microsec
                    TDA9887_AudioGain0;             // C7: normal audio gain
        bData[4] =  bAudioIF                     |  // E0 - E1: Sound IF
                    bVideoIF                     |  // E2 - E4: Video IF
                    TDA9887_TunerGainNormal      |  // E5: Tuner gain (normal)
                    TDA9887_Gating_18            |  // E6: Gating (18%)
                    TDA9887_AgcOutOFF;              // E7: VAGC  (off)
    }

    if (bPreSet)
    {
        bData[2] |= TDA9887_OutputPort2Inactive;
    }
    else
    {
        bData[2] &= ~TDA9887_OutputPort2Inactive;
    }

    dprintf3("TDA9885/6/7 Pinnacle: 0x%02x 0x%02x 0x%02x\n", bData[2],bData[3],bData[4]);

    I2CBus_Write(bData, 5);
}

static void TDA9887Pinnacle_Init(bool bPreInit, eVideoFormat videoFormat)
{
    TDA9887Pinnacle_TunerSet(bPreInit, videoFormat);
}

// ---------------------------------------------------------------------------
// CTDA9887Ex
//
typedef struct
{
    BYTE b;
    BYTE c;
    BYTE e;
} TTDABytes;

static const TTDABytes k_TDAStandardtSettings[TDA9887_FORMAT_LASTONE] =
{
    {
        // TDA9887_FORMAT_PAL_BG
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis50 | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_5_5  | TDA9887_VideoIF_38_90,
    },
    {
        // TDA9887_FORMAT_PAL_I
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis50 | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_6_0  | TDA9887_VideoIF_38_90,
    },
    {
        // TDA9887_FORMAT_PAL_DK
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis50 | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_6_5  | TDA9887_VideoIF_38_00,
    },
    {
        // TDA9887_FORMAT_PAL_MN
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis75 | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_4_5  | TDA9887_VideoIF_45_75,
    },
    {
        // TDA9887_FORMAT_SECAM_L
        TDA9887_PositiveAmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_6_5  | TDA9887_VideoIF_38_90,
    },
    {
        // TDA9887_FORMAT_SECAM_DK
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis50 | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_6_5  | TDA9887_VideoIF_38_00,
    },
    {
        // TDA9887_FORMAT_NTSC_M
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis50  | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_4_5  | TDA9887_VideoIF_45_75 | TDA9887_Gating_36,
    },
    {
        // TDA9887_FORMAT_NTSC_JP
        TDA9887_NegativeFmTV | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis50  | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_4_5  | TDA9887_VideoIF_58_75 | TDA9887_Gating_36,
    },
    {
        // TDA9887_FORMAT_RADIO
        TDA9887_FmRadio      | TDA9887_QSS | TDA9887_OutputPort1Inactive | TDA9887_OutputPort2Inactive,
        TDA9887_DeemphasisON | TDA9887_Deemphasis75 | TDA9887_TakeOverPointDefault,
        TDA9887_AudioIF_5_5  | TDA9887_RadioIF_38_90,
    },
};

static TTDABytes m_TDASettings[TDA9887_FORMAT_LASTONE];

// declarations for forward references
static void TDA9887Ex_SetBit(IN OUT BYTE * bits, IN BYTE bit, IN bool set);
static void TDA9887Ex_TunerSet(IN bool bPreSet, IN eVideoFormat format);
static void TDA9887Ex_TunerSetInt(IN bool bPreSet, IN eTDA9887Format format);
static eTDA9887Format TDA9887Ex_VideoFormat2TDA9887Format(IN eVideoFormat format);

static bool TDA9887Ex_CreateDetectedTDA9887Ex( void )
{
    static const BYTE tda9887Addresses[4] = { I2C_TDA9887_0, I2C_TDA9887_1,
                                              I2C_TDA9887_2, I2C_TDA9887_3 };
    uint  idx;

    for (idx = 0; idx < 4; idx++)
    {
        if (Tda9887_Detect(tda9887Addresses[idx]))
        {
           return TRUE;
        }
    }
    return FALSE;
}

static void TDA9887Ex_TunerInit( bool bPreInit, uint norm /* eVideoFormat videoFormat */ )
{
   TDA9887Ex_TunerSet(bPreInit, norm);
}

static void TDA9887Ex_TunerSet(IN bool bPreSet, IN eVideoFormat format)
{
    eTDA9887Format tdaFormat = TDA9887Ex_VideoFormat2TDA9887Format(format);
    if (tdaFormat == TDA9887_FORMAT_NONE)
    {
        dprintf1("CTDA9887Ex_TunerSet: Unsupported video format: %u", format);
    }
    else
    {
        TDA9887Ex_TunerSetInt(bPreSet, tdaFormat);
    }
}

static void TDA9887Ex_TunerSetInt(IN bool bPreSet, IN eTDA9887Format format)
{
    BYTE writeBytes[5];

    if (!bPreSet || format == TDA9887_FORMAT_NONE)
    {
        // Only setup before tuning (bPreSet).
        return;
    }
    if (format >= TDA9887_FORMAT_LASTONE)
    {
       debug1("CTDA9887Ex_TunerSet: invalid format %d\n", (int)format);
       return;
    }

    writeBytes[0] = Tda9887DeviceI2C;
    writeBytes[1] = 0;
    writeBytes[2] = m_TDASettings[format].b;
    writeBytes[3] = m_TDASettings[format].c;
    writeBytes[4] = m_TDASettings[format].e;

    dprintf3("CTDA9887Ex_TunerSet: 0x%02x 0x%02x 0x%02x\n", writeBytes[2], writeBytes[3], writeBytes[4]);
    I2CBus_Write(writeBytes, 5);
}

static void TDA9887Ex_SetModes(IN eTDA9887Format format, IN BYTE mask, IN BYTE bits)
{
    if (format == TDA9887_FORMAT_NONE)
    {
        return;
    }

    // Override demodulation
    if (mask & TDA9887_SM_CARRIER_QSS)
    {
        TDA9887Ex_SetBit(&m_TDASettings[format].b,
            TDA9887_QSS, (bits & TDA9887_SM_CARRIER_QSS) != 0);
    }
    // Override OutputPort1
    if (mask & TDA9887_SM_OUTPUTPORT1_INACTIVE)
    {
        TDA9887Ex_SetBit(&m_TDASettings[format].b,
            TDA9887_OutputPort1Inactive, (bits & TDA9887_SM_OUTPUTPORT1_INACTIVE) != 0);
    }
    // Override OutputPort2
    if (mask & TDA9887_SM_OUTPUTPORT2_INACTIVE)
    {
        TDA9887Ex_SetBit(&m_TDASettings[format].b,
            TDA9887_OutputPort2Inactive, (bits & TDA9887_SM_OUTPUTPORT2_INACTIVE) != 0);
    }
    // Override TakeOverPoint
    if (mask & TDA9887_SM_TAKEOVERPOINT_MASK)
    {
        m_TDASettings[format].c &= ~TDA9887_TakeOverPointMax;
        m_TDASettings[format].c |=
            (bits & TDA9887_SM_TAKEOVERPOINT_MASK) >> TDA9887_SM_TAKEOVERPOINT_OFFSET;
    }
}

static void TDA9887Ex_SetModesEx(IN TTDA9887FormatModes* modes)
{
    uint idx;

    memcpy(m_TDASettings, k_TDAStandardtSettings, sizeof(m_TDASettings));

    for (idx = 0; idx < TDA9887_FORMAT_LASTONE; idx++)
    {
        TDA9887Ex_SetModes(modes[idx].format, modes[idx].mask, modes[idx].bits);
    }
}

static eTDA9887Format TDA9887Ex_VideoFormat2TDA9887Format(IN eVideoFormat format)
{
    switch (format)
    {
        //case VIDEOFORMAT_PAL_B:
        //case VIDEOFORMAT_PAL_G:
        case VIDEO_MODE_PAL:
            return TDA9887_FORMAT_PAL_BG;
        //case VIDEOFORMAT_PAL_I:
        //    return TDA9887_FORMAT_PAL_I;
        //case VIDEOFORMAT_PAL_D:
        //    return TDA9887_FORMAT_PAL_DK;
        //case VIDEOFORMAT_PAL_M:
        //case VIDEOFORMAT_PAL_N:
        //case VIDEOFORMAT_PAL_N_COMBO:
        //    return TDA9887_FORMAT_PAL_MN;
        //case VIDEOFORMAT_SECAM_L:
        //case VIDEOFORMAT_SECAM_L1:
        case VIDEO_MODE_SECAM:
            return TDA9887_FORMAT_SECAM_L;
        //case VIDEOFORMAT_SECAM_D:
        //case VIDEOFORMAT_SECAM_K:
        //case VIDEOFORMAT_SECAM_K1:
        //    return TDA9887_FORMAT_SECAM_DK;
        //case VIDEOFORMAT_NTSC_M:
        case VIDEO_MODE_NTSC:
            return TDA9887_FORMAT_NTSC_M;
        //case VIDEOFORMAT_NTSC_50:
        //case VIDEOFORMAT_NTSC_M_Japan:
        //    return TDA9887_FORMAT_NTSC_JP;
        //case (VIDEOFORMAT_LASTONE+1):
        //    return TDA9887_FORMAT_RADIO;
        // I'm not sure about the following.
        //case VIDEOFORMAT_PAL_H:
        //case VIDEOFORMAT_SECAM_B:
        //case VIDEOFORMAT_SECAM_G:
        //case VIDEOFORMAT_SECAM_H:
        //    return TDA9887_FORMAT_PAL_BG;
        //case VIDEOFORMAT_PAL_60:
        //    return TDA9887_FORMAT_NTSC_M;
        default:
            debug1("TDA9887Ex: Invalid video format %d", (int)format);
            break;
    }
    // NEVER_GET_HERE;
    return TDA9887_FORMAT_NONE;
}

static void TDA9887Ex_SetBit(IN OUT BYTE * bits, IN BYTE bit, IN bool set)
{
    if (set)
    {
        *bits |= bit;
    }
    else
    {
        *bits &= ~bit;
    }
}

// ---------------------------------------------------------------------------
// The following code for MT2032 was taken verbatim from DScaler 4.01
// (but originates from bttv)
//

#define MT2032_OPTIMIZE_VCO 1   // perform VCO optimizations
static int  MT2032_XOGC = 4;    // holds the value of XOGC register after init
static bool MT2032_Locked = FALSE;

static BYTE MT2032_GetRegister( BYTE regNum )
{
   BYTE writeBuffer[2];
   BYTE readBuffer[1];
   BYTE result = 0;

   writeBuffer[0] = TunerDeviceI2C;
   writeBuffer[1] = regNum;

   if (pTvCard->i2cBus->I2cRead(pTvCard, writeBuffer, 2, readBuffer, 1))
   {
      result = readBuffer[0];
   }
   else
      debug1("MT2032-GetRegister: failed for sub addr 0x%02X", regNum);

   return result;
}

static void MT2032_SetRegister( BYTE regNum, BYTE data )
{
    BYTE buffer[3];

    buffer[0] = TunerDeviceI2C;
    buffer[1] = regNum;
    buffer[2] = data;

    I2CBus_Write(buffer, 3);
}

static bool MT2050_Initialize( void )
{
    /* Initialize Registers per spec. */
    MT2032_SetRegister(1, 0x2F);
    MT2032_SetRegister(2, 0x25);
    MT2032_SetRegister(3, 0xC1);
    MT2032_SetRegister(4, 0x00);
    MT2032_SetRegister(5, 0x63);
    MT2032_SetRegister(6, 0x11);
    MT2032_SetRegister(10, 0x85);
    MT2032_SetRegister(13, 0x28);
    MT2032_SetRegister(15, 0x0F);
    MT2032_SetRegister(16, 0x24);

#ifndef DPRINTF_OFF
    {
       BYTE rdbuf[2];
       rdbuf[0] = MT2032_GetRegister(13);
       dprintf1("mt2050 sro is %x\n", rdbuf[0]);
       if ((rdbuf[0] & 0x40) != 0)
           debug0("MT2050-Initialize: SRO Crystal problem - tuner will not function!");
    }
#endif

    return TRUE;
}

static int MT2050_SpurCheck(int flos1, int flos2, int fifbw, int fout)
{
    int n1 = 1, n2, f, nmax = 11;
    long Band;

    flos1 = flos1 / 1000;     /* scale to kHz to avoid 32bit overflows */
    flos2 = flos2 / 1000;
    fifbw /= 1000;
    fout /= 1000;

    Band = fout + fifbw / 2;

    do {
        n2 = -n1;
        f = n1 * (flos1 - flos2);
        do {
            n2--;
            f = f - flos2;
            if (abs((abs(f) - fout)) < (fifbw >> 1))
            {
                return 1;
            }
        } while ((f > (flos2 - fout - (fifbw >> 1))) && (n2 > -nmax));
        n1++;
    } while (n1 < nmax);

    return 0;
}

static bool MT2050_SetIFFreq(int rfin)
{
    static const uint if1=1218*1000*1000;
    static const uint if2=38900 * 1000;
    unsigned char   buf[5];

    long flo1, flo2;
    int n = 0;
    long flos1, flos2, fifbw, fif1_bw;
    long ftest;
    char SpurInBand;

    long LO1I, LO2I, flo1step, flo2step;
    long flo1rem, flo2rem, flo1tune, flo2tune;
    int num1, num2, Denom1, Denom2;
    int div1a, div1b, div2a, div2b;

    dprintf1("MT2050-SetIFFreq: rfin=%d\n", rfin);

    //3.1 Calculate LO frequencies
    flo1 = rfin + if1;
    flo1 = flo1 / 1000000;
    flo1 = flo1 * 1000000;
    flo2 = flo1 - rfin - if2;

    //3.2 Avoid spurs
    flos1 = flo1;
    flos2 = flo2;
    fif1_bw = 16000000;
    fifbw = 8750000;  /* PAL */

    do {
        if ((n & 1) == 0)
        {
            flos1 = flos1 - 1000000 * n;
            flos2 = flos2 - 1000000 * n;
        }
        else
        {
            flos1 = flos1 + 1000000 * n;
            flos2 = flos2 + 1000000 * n;
        }
        //check we are still in bandwidth
        ftest = abs(flos1 - rfin - if1 + (fifbw >> 1));
        if (ftest > (fif1_bw >> 1))
        {
            flos1 = flo1;
            flos2 = flo2;
            debug0("MT2050-SetIFFreq: No spur");
            break;
        }
        n++;
        SpurInBand = MT2050_SpurCheck(flos1, flos2, fifbw, if2);
    } while(SpurInBand != 0);

    flo1 = flos1;
    flo2 = flos2;

    //3.3 Calculate LO registers

    flo1step = 1000000;
    flo2step = 50000;
    LO1I = floor(flo1 / 4000000.0);
    LO2I = floor(flo2 / 4000000.0);
    flo1rem = flo1 % 4000000;
    flo2rem = flo2 % 4000000;
    flo1tune = flo1step * floor((flo1rem + flo1step / 2.0) / flo1step);
    flo2tune = flo2step * floor((flo2rem + flo2step / 2.0) / flo2step);
    Denom1 = 4;
    Denom2 = 4095;
    num1 = floor(flo1tune / (4000000.0 / Denom1) + 0.5);
    num2 = floor(flo2tune / (4000000.0 / Denom2) + 0.5);
    if (num1 >= Denom1)
    {
        num1 = 0;
        LO1I++;
    }
    if (num2 >= Denom2)
    {
        num2 = 0;
        LO2I++;
    }
    div1a = floor(LO1I / 12) - 1;
    div1b = LO1I % 12;
    div2a = floor(LO2I / 8) - 1;
    div2b = LO2I % 8;

    //3.4 Writing registers
    if (rfin < 277000000)
    {
        buf[0] = 128 + 4 * div1b + num1;
    }
    else
    {
        buf [0] = 4 * div1b + num1;
    }
    buf [1] = div1a;
    buf [2] = 32 * div2b + floor(num2 / 256.0);
    buf [3] = num2 % 256;
    if (num2 == 0)
    {
        buf [4] = div2a;
    }
    else
    {
        buf [4] = 64 + div2a;
    }
#ifndef DPRINTF_OFF
    { 
        int i;
        dprintf0("MT2050 registers #1-5 write: ");
        for (i=0; i<=4; i++)
            dprintf1("%x ", buf[i]);
        dprintf0("\n");
    }
#endif

    MT2032_SetRegister(1, buf[0x00]);
    MT2032_SetRegister(2, buf[0x01]);
    MT2032_SetRegister(3, buf[0x02]);
    MT2032_SetRegister(4, buf[0x03]);
    MT2032_SetRegister(5, buf[0x04]);

    {   //3.5 Allow LO to lock
        int nlock = 0, Status;
        bool m_Locked;

        m_Locked = FALSE;
        Sleep(50);
        do {
            Status = MT2032_GetRegister(7);
            Status &= 0x88;
            if (Status == 0x88)
            {
                m_Locked = TRUE;
                break;
            }
            Sleep(2);
            nlock++;
        } while(nlock < 100);
    }
    return TRUE;
}

static bool MT2032_Initialize( void )
{
    int  xogc, xok = 0;

    /* Initialize Registers per spec. */
    MT2032_SetRegister(2, 0xff);
    MT2032_SetRegister(3, 0x0f);
    MT2032_SetRegister(4, 0x1f);
    MT2032_SetRegister(6, 0xe4);
    MT2032_SetRegister(7, 0x8f);
    MT2032_SetRegister(8, 0xc3);
    MT2032_SetRegister(9, 0x4e);
    MT2032_SetRegister(10, 0xec);
    MT2032_SetRegister(13, 0x32);

    /* Adjust XOGC (register 7), wait for XOK */
    xogc = 7;
    do
    {
        Sleep(10);
        xok = MT2032_GetRegister(0x0e) & 0x01;
        if (xok == 1)
        {
            break;
        }
        xogc--;
        if (xogc == 3)
        {
            xogc = 4;   /* min. 4 per spec */
            break;
        }
        MT2032_SetRegister(7, 0x88 + xogc);
    } while (xok != 1);

    MT2032_XOGC = xogc;
    return TRUE;
}


static bool Microtune_Initialize( TUNER_TYPE type, uint defaultNorm )
{
    BYTE rdbuf[22];
    int  i;
    uint company_code;
    bool result;

    dprintf2("Microtune-Initialize: default type=%d, norm %d\n", type, defaultNorm);

    if (isTda9887Ex)
        TDA9887Ex_TunerInit(TRUE, defaultNorm);
    else if (haveTda9887Standard)
        Tda9887_Init(TRUE, defaultNorm);
    else if (haveTda9887Pinnacle)
        TDA9887Pinnacle_Init(TRUE, defaultNorm);

    for (i = 0; i < 21; i++)
    {
        rdbuf[i] = MT2032_GetRegister(i);
    }

    dprintf4("Microtune: Companycode=%02x%02x Part=%02x Revision=%02x\n", rdbuf[0x11],rdbuf[0x12],rdbuf[0x13],rdbuf[0x14]);

    company_code = (rdbuf[0x11] << 8) | rdbuf[0x12];
    if ((company_code != 0x4d54) && (company_code != 0x3cbf))
        debug1("Microtune-Initialize: unknown company code 0x%04x", company_code);

    MicrotuneType = rdbuf[0x13];
    switch (MicrotuneType)
    {
        case MT2032:
            dprintf0("tuner: MT2032 detected.\n");
            result = MT2032_Initialize();
            break;

        case MT2050:
            dprintf0("tuner: MT2050 detected.\n");
            result = MT2050_Initialize();
            break;

        default:
            debug1("Microtune-Initialize: unknown tuner type 0x%02x", MicrotuneType);
            MicrotuneType = ((type == TUNER_MT2050) || (type == TUNER_MT2050_PAL)) ? MT2050 : MT2032;
            if (MicrotuneType == MT2032)
                result = MT2032_Initialize();
            else
                result = MT2050_Initialize();
            break;
    }

    if (isTda9887Ex)
        TDA9887Ex_TunerInit(TRUE, defaultNorm);
    else if (haveTda9887Standard)
        Tda9887_Init(FALSE, defaultNorm);
    else if (haveTda9887Pinnacle)
        TDA9887Pinnacle_Init(FALSE, defaultNorm);

    return result;
}

static int MT2032_SpurCheck(int f1, int f2, int spectrum_from, int spectrum_to)
{
    int n1 = 1, n2, f;

    f1 = f1 / 1000;     /* scale to kHz to avoid 32bit overflows */
    f2 = f2 / 1000;
    spectrum_from /= 1000;
    spectrum_to /= 1000;

    do
    {
        n2 = -n1;
        f = n1 * (f1 - f2);
        do
        {
            n2--;
            f = f - f2;
            if ((f > spectrum_from) && (f < spectrum_to))
            {
                return 1;
            }
        } while ((f > (f2 - spectrum_to)) || (n2 > -5));
        n1++;
    } while (n1 < 5);

    return 0;
}

static bool MT2032_ComputeFreq(
                            int             rfin,
                            int             if1,
                            int             if2,
                            int             spectrum_from,
                            int             spectrum_to,
                            unsigned char   *buf,
                            int             *ret_sel,
                            int             xogc
                        )   /* all in Hz */
{
    int fref, lo1, lo1n, lo1a, s, sel;
    int lo1freq, desired_lo1, desired_lo2, lo2, lo2n, lo2a, lo2num, lo2freq;
    int nLO1adjust;

    fref = 5250 * 1000; /* 5.25MHz */

    /* per spec 2.3.1 */
    desired_lo1 = rfin + if1;
    lo1 = (2 * (desired_lo1 / 1000) + (fref / 1000)) / (2 * fref / 1000);
    lo1freq = lo1 * fref;
    desired_lo2 = lo1freq - rfin - if2;

    /* per spec 2.3.2 */
    for (nLO1adjust = 1; nLO1adjust < 3; nLO1adjust++)
    {
        if (!MT2032_SpurCheck(lo1freq, desired_lo2, spectrum_from, spectrum_to))
        {
            break;
        }

        if (lo1freq < desired_lo1)
        {
            lo1 += nLO1adjust;
        }
        else
        {
            lo1 -= nLO1adjust;
        }

        lo1freq = lo1 * fref;
        desired_lo2 = lo1freq - rfin - if2;
    }

    /* per spec 2.3.3 */
    s = lo1freq / 1000 / 1000;

    if (MT2032_OPTIMIZE_VCO)
    {
        if (s > 1890)
        {
            sel = 0;
        }
        else if (s > 1720)
        {
            sel = 1;
        }
        else if (s > 1530)
        {
            sel = 2;
        }
        else if (s > 1370)
        {
            sel = 3;
        }
        else
        {
            sel = 4;    /* >1090 */
        }
    }
    else
    {
        if (s > 1790)
        {
            sel = 0;    /* <1958 */
        }
        else if (s > 1617)
        {
            sel = 1;
        }
        else if (s > 1449)
        {
            sel = 2;
        }
        else if (s > 1291)
        {
            sel = 3;
        }
        else
        {
            sel = 4;    /* >1090 */
        }
    }

    *ret_sel = sel;

    /* per spec 2.3.4 */
    lo1n = lo1 / 8;
    lo1a = lo1 - (lo1n * 8);
    lo2 = desired_lo2 / fref;
    lo2n = lo2 / 8;
    lo2a = lo2 - (lo2n * 8);
    lo2num = ((desired_lo2 / 1000) % (fref / 1000)) * 3780 / (fref / 1000); /* scale to fit in 32bit arith */
    lo2freq = (lo2a + 8 * lo2n) * fref + lo2num * (fref / 1000) / 3780 * 1000;

    if (lo1a < 0 ||  
        lo1a > 7 ||  
        lo1n < 17 ||  
        lo1n > 48 ||  
        lo2a < 0 ||  
        lo2a > 7 ||  
        lo2n < 17 ||  
        lo2n > 30)
    {
        debug5("MT2032-ComputeFreq: rfin=%d: invalid LO: %d,%d,%d,%d", rfin, lo1a, lo1n, lo2a, lo2n);
        return FALSE;
    }

    /* set up MT2032 register map for transfer over i2c */
    buf[0] = lo1n - 1;
    buf[1] = lo1a | (sel << 4);
    buf[2] = 0x86;                  /* LOGC */
    buf[3] = 0x0f;                  /* reserved */
    buf[4] = 0x1f;
    buf[5] = (lo2n - 1) | (lo2a << 5);
    if (rfin < 400 * 1000 * 1000)
    {
        buf[6] = 0xe4;
    }
    else
    {
        buf[6] = 0xf4;              /* set PKEN per rev 1.2 */
    }

    buf[7] = 8 + xogc;
    buf[8] = 0xc3;                  /* reserved */
    buf[9] = 0x4e;                  /* reserved */
    buf[10] = 0xec;                 /* reserved */
    buf[11] = (lo2num & 0xff);
    buf[12] = (lo2num >> 8) | 0x80; /* Lo2RST */

    return TRUE;
}

static int MT2032_CheckLOLock(void)
{
    int t, lock = 0;
    for (t = 0; t < 10; t++)
    {
        lock = MT2032_GetRegister(0x0e) & 0x06;
        if (lock == 6)
        {
            break;
        }
        Sleep(1);
    }
    return lock;
}

static int MT2032_OptimizeVCO(int sel, int lock)
{
    int tad1, lo1a;

    tad1 = MT2032_GetRegister(0x0f) & 0x07;

    if (tad1 == 0)
    {
        return lock;
    }
    if (tad1 == 1)
    {
        return lock;
    }
    if (tad1 == 2)
    {
        if (sel == 0)
        {
            return lock;
        }
        else
        {
            sel--;
        }
    }
    else
    {
        if (sel < 4)
        {
            sel++;
        }
        else
        {
            return lock;
        }
    }
    lo1a = MT2032_GetRegister(0x01) & 0x07;
    MT2032_SetRegister(0x01, lo1a | (sel << 4));
    lock = MT2032_CheckLOLock();

    dprintf1("MT2032-OptimizeVCO: lock=%d\n", lock);
    return lock;
}

static bool MT2032_SetIFFreq(int rfin, int if1, int if2, int from, int to, uint norm )
{
    uchar   buf[21];
    int     lint_try, sel, lock = 0;
    bool    result = FALSE;

    if ( MT2032_ComputeFreq(rfin, if1, if2, from, to, &buf[0], &sel, MT2032_XOGC) )
    {
        dprintf4("MT2032-SetIFFreq: 0x%02X%02X%02X%02X...\n", buf[0x00], buf[0x01], buf[0x02], buf[0x03]);

        /* send only the relevant registers per Rev. 1.2 */
        MT2032_SetRegister(0, buf[0x00]);
        MT2032_SetRegister(1, buf[0x01]);
        MT2032_SetRegister(2, buf[0x02]);

        MT2032_SetRegister(5, buf[0x05]);
        MT2032_SetRegister(6, buf[0x06]);
        MT2032_SetRegister(7, buf[0x07]);

        MT2032_SetRegister(11, buf[0x0B]);
        MT2032_SetRegister(12, buf[0x0C]);

        /* wait for PLLs to lock (per manual), retry LINT if not. */
        for (lint_try = 0; lint_try < 2; lint_try++)
        {
            lock = MT2032_CheckLOLock();

            if (MT2032_OPTIMIZE_VCO)
            {
                lock = MT2032_OptimizeVCO(sel, lock);
            }

            if (lock == 6)
            {
                break;
            }

            /* set LINT to re-init PLLs */
            MT2032_SetRegister(7, 0x80 + 8 + MT2032_XOGC);
            Sleep(10);
            MT2032_SetRegister(7, 8 + MT2032_XOGC);
        }

        MT2032_SetRegister(2, 0x20);

        MT2032_Locked = (lock == 6);

        result = TRUE;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Set TV tuner synthesizer frequency
//
bool Tuner_SetFrequency( TUNER_TYPE type, uint wFrequency, uint norm )
{
   TTunerType tun;
   double dFrequency;
   BYTE buffer[5];
   BYTE config;
   WORD div;
   bool result;

   static uint lastWFreq = 0;

   if (type == TUNER_ABSENT)
   {
      result = FALSE;
   }
   else if ((TunerDeviceI2C == 0) || (pTvCard == NULL) || (pTvCard->i2cBus == NULL))
   {
      debug2("Tuner-SetFrequency: called when not initialized (type=%d I2C=%d)", type, TunerDeviceI2C);
      result = FALSE;
   }
   else if ((type < TUNERS_COUNT) && (type != TUNER_ABSENT))
   {
      dprintf4("Tuner-SetFrequency: type=%d, freq=%d (last=%d), norm=%d\n", type, wFrequency, lastWFreq, norm);

      I2CBus_Lock();

      if (isTda9887Ex)
         TDA9887Ex_TunerSet(TRUE, norm);
      else if (haveTda9887Standard)
         Tda9887_TunerSet(TRUE, norm);
      else if (haveTda9887Pinnacle)
         TDA9887Pinnacle_TunerSet(TRUE, norm);

      if ( (type == TUNER_MT2032) || (type == TUNER_MT2032_PAL) ||
           (type == TUNER_MT2050) || (type == TUNER_MT2050_PAL) )
      {
         // XXX TODO: norm handling: use initial norm for now
         norm = m_LastVideoFormat;

         if (MicrotuneType == MT2032)
         {
            result = MT2032_SetIFFreq(wFrequency * 62500, 1090 * 1000 * 1000, 38900 * 1000, 32900 * 1000, 39900 * 1000, norm);
         }
         else
         {
            result = MT2050_SetIFFreq(wFrequency * 62500);
         }
      }
      else if (type == TUNER_TDA8275)
      {
         result = Tda8275_SetTVFrequency(wFrequency * 62500, norm);
      }
      else
      {
         if ((wFrequency < 44*16) || (wFrequency > 958*16))
            debug1("Tuner-SetFrequency: freq %d/16 is out of range", wFrequency);

         Tuner_GetParams(type, &tun);
         dFrequency = (double) wFrequency;

         if (dFrequency < tun.thresh1)
            config = tun.vhf_l;
         else if (dFrequency < tun.thresh2)
            config = tun.vhf_h;
         else
            config = tun.uhf;

         // tv norm specification for multi-norm tuners
         switch (type)
         {
            case TUNER_PHILIPS_SECAM:
               /* XXX TODO: disabled until norm is provided by TV channel file parsers
               if (norm == VIDEO_MODE_SECAM)
                  config |= 0x02;
               else
                  config &= ~0x02;
               */
               break;
            case TUNER_TEMIC_4046FM5_MULTI:
               config &= ~0x0f;
               /*
               if (norm == VIDEO_MODE_SECAM)
                  config |= TEMIC_SET_PAL_L;
               else
               */
                  config |= TEMIC_SET_PAL_BG;
               break;
            case TUNER_PHILIPS_MULTI:
               config &= ~0x0f;
               /*
               if (norm == VIDEO_MODE_SECAM)
                  config |= PHILIPS_SET_PAL_L;
               else
               */
                  config |= PHILIPS_SET_PAL_BGDK;
               break;
            default:
               break;
         }

         div = (WORD)dFrequency + tun.IfpCoff;

         if ((type == TUNER_PHILIPS_SECAM) && (wFrequency < lastWFreq))
         {
            /* Philips specification says to send config data before
            ** frequency in case (wanted frequency < current frequency) */
            buffer[0] = TunerDeviceI2C;
            buffer[1] = tun.config;
            buffer[2] = config;
            buffer[3] = (div>>8) & 0x7f;
            buffer[4] = div      & 0xff;
         }
         else
         {
            buffer[0] = TunerDeviceI2C;
            buffer[1] = (div>>8) & 0x7f;
            buffer[2] = div      & 0xff;
            buffer[3] = tun.config;
            buffer[4] = config;
         }
         lastWFreq = wFrequency;

         result = I2CBus_Write(buffer, 5);
      }

      if (isTda9887Ex)
         TDA9887Ex_TunerSet(FALSE, norm);
      else if (haveTda9887Standard)
         Tda9887_TunerSet(FALSE, norm);
      else if (haveTda9887Pinnacle)
         TDA9887Pinnacle_TunerSet(FALSE, norm);

      I2CBus_Unlock();
   }
   else
   {
      ifdebug1(type != TUNER_ABSENT, "Tuner-SetFrequency: illegal tuner idx %d", type);
      result = FALSE;
   }

   return result;
}

// ----------------------------------------------------------------------------
// Retrieve identifier strings for supported tuner types
// - called by user interface
//
const char * Tuner_GetName( uint idx )
{
   if (idx < TUNERS_COUNT)
      return TunerNames[idx];
   else
      return NULL;
}

// ----------------------------------------------------------------------------
// Query Hauppauge EEPROM tuner ID
//
eTunerId Tuner_GetHauppaugeEepromId( uint idx )
{
   eTunerId Tuner = TUNER_ABSENT;

   if (idx < sizeof(m_TunerHauppaugeAnalog)/sizeof(m_TunerHauppaugeAnalog[0]))
   {
      Tuner = m_TunerHauppaugeAnalog[idx];
      dprintf2("Tuner-GetHauppaugeEepromId: ID:0x%02X -> %d\n", idx, (int)Tuner);
   }
   else
      debug1("Tuner-GetHauppaugeEepromId: invalid ID 0x%X", idx);

   return Tuner;
}

// ---------------------------------------------------------------------------
// Free module resources
//
void Tuner_Close( void )
{
   pTvCard = NULL;
   TunerDeviceI2C = 0;
}

// ---------------------------------------------------------------------------
// Auto-detect a tuner on the I2C bus
//
bool Tuner_Init( TUNER_TYPE type, TVCARD * pNewTvCardIf )
{
   TTDA9887FormatModes * pTda9887Modes;
   uint i2cStart;
   BYTE i2cPort;
   uint defaultNorm = 0;
   bool result = FALSE;

   dprintf1("Tuner-Init: requested type %d\n", type);
   TunerDeviceI2C = 0;
   pTvCard = NULL;

   if ((pNewTvCardIf != NULL) && (pNewTvCardIf->i2cBus != NULL))
   {
      pTvCard               = pNewTvCardIf;
      haveTda9887Standard   = FALSE;
      haveTda9887Pinnacle   = FALSE;
      isTda9887PinnacleMono = FALSE;
      isTda9887Ex           = FALSE;
      haveTda8290           = FALSE;

      if (type == TUNER_ABSENT)
      {
         result = TRUE;
      }
      else if (type < TUNERS_COUNT)
      {
         I2CBus_Lock();

         if ((type == TUNER_MT2032) || (type == TUNER_MT2032_PAL) ||
             (type == TUNER_MT2050) || (type == TUNER_MT2050_PAL) ||
             (type == TUNER_PHILIPS_FM1216ME_MK3))
         {
            if ((type != TUNER_MT2032_PAL) && (type != TUNER_MT2050_PAL))
               defaultNorm = VIDEO_MODE_SECAM;
            else
               defaultNorm = VIDEO_MODE_PAL;
            dprintf1("Tuner-Init: detecting IF demodulator, norm %d\n", defaultNorm);

            if (pTvCard->cfg->GetTda9887Modes(pTvCard, &haveTda9887Standard, (void **)&pTda9887Modes))
            {
               // presence of TDA9887 isdefined in the card INI file
               if (haveTda9887Standard)
               {
                  haveTda9887Standard = FALSE;  //overridden by "Ex" flag
                  if ( TDA9887Ex_CreateDetectedTDA9887Ex() )
                  {
                     isTda9887Ex = TRUE;
                     TDA9887Ex_SetModesEx(pTda9887Modes);
                     TDA9887Ex_TunerInit(TRUE, defaultNorm);
                  }
                  else
                     debug0("Tuner-Init: failed to detect configured TDA9887");
               }
            }
            else
            {  // legacy auto-detection mode - only used by BT8x8 driver now
               // detect and initialize external IF demodulator (must be done before port scan)
               pTvCard->cfg->GetIffType(pTvCard, &haveTda9887Pinnacle, &isTda9887PinnacleMono);
               if ( haveTda9887Pinnacle && Tda9887_Detect(I2C_TDA9887_0) )
               {
                  haveTda9887Pinnacle = TRUE;
                  TDA9887Pinnacle_Init(TRUE, defaultNorm);
               }
               else if ( Tda9887_Detect(I2C_TDA9887_0) ||
                         Tda9887_Detect(I2C_TDA9887_1) )
               {
                  haveTda9887Standard  = TRUE;
                  Tda9887_Init(TRUE, defaultNorm);
               }
            }
         }
         else if (type == TUNER_TDA8275)
         {
            if ( Tda8290_Detect() )
            {
               haveTda8290 = TRUE;
               Tda8290_Init(TRUE, VIDEO_MODE_PAL);  // norm is unused
            }
            else
               debug0("Tuner-Init: failed to detect TDA8290 - expected to be present with TDA8275");
         }

         // Try to detect TEA5767 FM-Radio chip 
         // if present, don't scan at address 0xC0, else the EEPROM can get corrupted
         if (IsTEA5767PresentAtC0(pTvCard))
            i2cStart = 0xC2;
         else
            i2cStart = 0xC0;

         // scan the I2C bus for devices
         dprintf1("Tuner-Init: checking for tuner at I2C addr 0x%02X...0xCE (step 2)\n", i2cStart);
         result = FALSE;
         for (i2cPort = i2cStart; (i2cPort <= 0xCE) && !result; i2cPort += 2)
         {
            if ( pTvCard->i2cBus->I2cWrite(pTvCard, &i2cPort, 1) )
            {
               dprintf1("Tuner-Init: found I2C device at 0x%02X\n", i2cPort);
               TunerDeviceI2C = i2cPort;

               if ( (type == TUNER_MT2032) || (type == TUNER_MT2032_PAL) ||
                    (type == TUNER_MT2050) || (type == TUNER_MT2050_PAL) )
               {
                  result = Microtune_Initialize(type, defaultNorm);
                  // note: continue loop if tuner not identified
               }
               else if (type == TUNER_TDA8275)
               {
                  result = Tda8275_InitializeTuner();
               }
               else
               {  // "simple" tuner found -> done
                  result = TRUE;
               }
            }
         }

         if (result == FALSE)
         {
            TunerDeviceI2C = 0;
            MessageBox(NULL, "Warning: No TV tuner was found during the I2C bus "
                             "scan. You either have selected the wrong tuner type "
                             "in the TV card configuration, or your card's tuner "
                             "is not supported. Acquisition will probably not "
                             "work (cannot switch TV channels. Configure "
                             "\"No tuner/Unknown\" to avoid this message. "
                             "See README.txt for more info.",
                             "nxtvepg driver problem",
                             MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
            debug0("Tuner-Init: no tuner found - disabling module");
         }
         I2CBus_Unlock();
      }
      else
         debug1("Tuner-Init: illegal tuner idx %d", type);
   }
   else
      fatal2("Tuner-Init: illegal NULL ptr params %lX, %lX", (long)pNewTvCardIf, (long)((pNewTvCardIf != NULL) ? pNewTvCardIf->i2cLineBus : NULL));

   return result;
}

