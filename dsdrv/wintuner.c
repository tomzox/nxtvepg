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
 *      DScaler parts are copyleft 2001 itt@myself.com
 *
 *  DScaler #Id: GenericTuner.cpp,v 1.13 2003/10/27 10:39:51 adcockj Exp #
 *  DScaler #Id: TunerID.cpp,v 1.3 2003/12/18 15:57:41 adcockj Exp #
 *  DScaler #Id: MT2032.cpp,v 1.13 2004/01/14 17:06:44 robmuller Exp #
 *  DScaler #Id: MT2050.cpp,v 1.5 2004/04/06 12:20:48 adcockj Exp #
 *  DScaler #Id: TDA9887.cpp,v 1.9 2004/09/29 20:36:02 to_see Exp #
 *
 *  $Id: wintuner.c,v 1.23 2004/12/26 21:48:15 tom Exp tom $
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
#include "dsdrv/wintuner.h"


// ---------------------------------------------------------------------------
// Module state variables

static BYTE TunerDeviceI2C = 0;
static eTDA9887Card Tda9887Type;
static CRITICAL_SECTION m_cCrit;    //semaphore for I2C access
static uint MicrotuneType;
static bool haveTda9887Standard;
static bool haveTda9887Pinnacle;
static bool isTda9887PinnacleMono;
static BYTE Tda9887DeviceI2C = 0;
static uint m_LastVideoFormat = 0xff;
static TVCARD * pTvCard = NULL;

// ---------------------------------------------------------------------------
// Tuner table
//
static const char * TunerNames[TUNER_LASTONE] =
{
   "*No Tuner/Unknown*",                        // TUNER_ABSENT = 0
   "Philips [PAL_I]",                           // TUNER_PHILIPS_PAL_I      
   "Philips [NTSC]",                            // TUNER_PHILIPS_NTSC     
   "Philips [SECAM]",                           // TUNER_PHILIPS_SECAM      
   "Philips [PAL]",                             // TUNER_PHILIPS_PAL
   "Temic 4002 FH5 [PAL B/G]",                  // TUNER_TEMIC_4002FH5_PAL
   "Temic 4032 FY5 [NTSC]",                     // TUNER_TEMIC_4032FY5_NTSC
   "Temic 4062 FY5 [PAL I]",                    // TUNER_TEMIC_4062FY5_PAL_I
   "Temic 4036 FY5 [NTSC]",                     // TUNER_TEMIC_4036FY5_NTSC     
   "Alps TSBH1 [NTSC]",                         // TUNER_ALPS_TSBH1_NTSC                             
   "Alps TSBE1 [PAL]",                          // TUNER_ALPS_TSBE1_PAL                                    
   "Alps TSBB5 [PAL I]",                        // TUNER_ALPS_TSBB5_PAL_I                                  
   "Alps TSBE5 [PAL]",                          // TUNER_ALPS_TSBE5_PAL                                    
   "Alps TSBC5 [PAL]",                          // TUNER_ALPS_TSBC5_PAL                                    
   "Temic 4006 FH5 [PAL B/G]",                  // TUNER_TEMIC_4006FH5_PAL      
   "Philips 1236D Input 1 [ATSC/NTSC]",         // TUNER_PHILIPS_1236D_NTSC_INPUT1
   "Philips 1236D Input 2 [ATSC/NTSC]",         // TUNER_PHILIPS_1236D_NTSC_INPUT2
   "Alps TSCH6 [NTSC]",                         // TUNER_ALPS_TSCH6_NTSC                                     
   "Temic 4016 FY5 [PAL D/K/L]",                // TUNER_TEMIC_4016FY5_PAL
   "Philips MK2           [NTSC_M]",            // TUNER_PHILIPS_MK2_NTSC     
   "Temic 4066 FY5 [PAL I]",                    // TUNER_TEMIC_4066FY5_PAL_I
   "Temic 4006 FN5 [PAL Auto]",                 // TUNER_TEMIC_4006FN5_PAL
   "Temic 4009 FR5 [PAL B/G] + FM",             // TUNER_TEMIC_4009FR5_PAL
   "Temic 4039 FR5 [NTSC] + FM",                // TUNER_TEMIC_4039FR5_NTSC
   "Temic 4046 FM5 [PAL/SECAM multi]",          // TUNER_TEMIC_4046FM5_MULTI
   "Philips [PAL_DK]",                          // TUNER_PHILIPS_PAL_DK
   "Philips FQ1216ME      [PAL/SECAM multi]",   // TUNER_PHILIPS_MULTI      
   "LG TAPC-I001D [PAL I] + FM",                // TUNER_LG_I001D_PAL_I
   "LG TAPC-I701D [PAL I]",                     // TUNER_LG_I701D_PAL_I
   "LG TPI8NSR01F [NTSC] + FM",                 // TUNER_LG_R01F_NTSC
   "LG TPI8PSB01D [PAL B/G] + FM",              // TUNER_LG_B01D_PAL
   "LG TPI8PSB11D [PAL B/G]",                   // TUNER_LG_B11D_PAL      
   "Temic 4009 FN5 [PAL Auto] + FM",            // TUNER_TEMIC_4009FN5_PAL
   "MT2032 universal [SECAM default]",          // TUNER_MT2032
   "Sharp 2U5JF5540 [NTSC_JP]",                 // TUNER_SHARP_2U5JF5540_NTSC
   "LG TAPC-H701P [NTSC]",                      // TUNER_LG_TAPCH701P_NTSC
   "Samsung TCPM9091PD27 [PAL B/G/I/D/K]",      // TUNER_SAMSUNG_PAL_TCPM9091PD27
   "Temic 4106 FH5 [PAL B/G]",                  // TUNER_TEMIC_4106FH5  
   "Temic 4012 FY5 [PAL D/K/L]",                // TUNER_TEMIC_4012FY5      
   "Temic 4136 FY5 [NTSC]",                     // TUNER_TEMIC_4136FY5
   "LG TAPC-new   [PAL]",                       // TUNER_LG_TAPCNEW_PAL     
   "Philips FQ1216ME MK3  [PAL/SECAM multi]",   // TUNER_PHILIPS_FM1216ME_MK3
   "LG TAPC-new   [NTSC]",                      // TUNER_LG_TAPCNEW_NTSC
   "MT2032 universal [PAL default]",            // TUNER_MT2032_PAL
   "Philips FI1286 [NTCS M-J]",                 // TUNER_PHILIPS_FI1286_NTSC_M_J
   "MT2050 [SECAM default]",                    // TUNER_MT2050
   "MT2050 [PAL default]",                      // TUNER_MT2050_PAL
   "Philips 4in1 [ATI TV Wonder Pro/Conexant]", // TUNER_PHILIPS_4IN1
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
    }
}

// ---------------------------------------------------------------------------
// Generic I2C interface: pass calls through to hardware-specific driver
//
static void I2CBus_Lock( void )
{
   EnterCriticalSection(&m_cCrit);
}

static void I2CBus_Unlock( void )
{
   LeaveCriticalSection(&m_cCrit);
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

#define I2C_TDA9887_x         0           // placehoder only
#define I2C_TDA9887_0      0x86
#define I2C_TDA9887_1      0x96

typedef struct
{
    DWORD eTDA9887Card;
    BYTE Pal_BG[3];
    //BYTE Pal_I[3];
    //BYTE Pal_DK[3];
    BYTE Secam_L[3];
    BYTE Ntsc[3];
    //BYTE Ntsc_Jp[3];
    //BYTE Fm_Radio[3];

} TDA9887_ControlSettings;


static const TDA9887_ControlSettings Tda9887TypeSettings[TDA9887_LASTONE] =
{
    {
        TDA9887_DEFAULT,
        // B ,   C,    E
        {0x96, 0x70, 0x49}, // PAL_BG
        //{0x96, 0x70, 0x4a}, // PAL_I
        //{0x96, 0x70, 0x4b}, // PAL_DK
        {0x86, 0x50, 0x4b}, // SECAM_L
        {0x96, 0x70, 0x44}, // NTSC
        //{0x96, 0x70, 0x40}, // NTSC_JP
        //{0x8e, 0x0d, 0x77}, // FM_RADIO
    },
    {
        TDA9887_MSI_TV_ANYWHERE_MASTER,
        {0x56, 0x70, 0x49}, // PAL_BG       Working
        //{0x56, 0x6e, 0x4a}, // PAL_I        Working
        //{0x56, 0x70, 0x4b}, // PAL_DK       Working
        {0x86, 0x50, 0x4b}, // SECAM_L      ?
        {0x92, 0x30, 0x04}, // NTSC         Working
        //{0x92, 0x30, 0x40}, // NTSC_JP      ?
        //{0x8e, 0x0d, 0x77}, // FM_RADIO     ?
    },
    {
        TDA9887_LEADTEK_WINFAST_EXPERT,
        {0x16, 0x70, 0x49}, // PAL_BG       Working
        //{0x16, 0x6e, 0x4a}, // PAL_I        ?
        //{0x16, 0x70, 0x4b}, // PAL_DK       Working
        {0x86, 0x50, 0x4b}, // SECAM_L      ?
        {0x92, 0x30, 0x04}, // NTSC         ?
        //{0x92, 0x30, 0x40}, // NTSC_JP      ?
        //{0x8e, 0x70, 0x49}, // FM_RADIO     ? must work
    },
    {
        TDA9887_ATI_TV_WONDER_PRO,
        {0x16, 0x70, 0x49}, // PAL_BG       ?
        //{0x16, 0x6e, 0x4a}, // PAL_I        ?
        //{0x16, 0x70, 0x4b}, // PAL_DK       ?
        {0x86, 0x50, 0x4b}, // SECAM_L      ?
        {0x92, 0x30, 0x04}, // NTSC         Working
        //{0x92, 0x30, 0x40}, // NTSC_JP      ?
        //{0x8e, 0x0d, 0x77}, // FM_RADIO     ?
    },
    {
        TDA9887_AVERTV_303,
        {0x16, 0x70, 0x49}, // PAL_BG       ?
        //{0x16, 0x6e, 0x4a}, // PAL_I        ?
        //{0x16, 0x70, 0x4b}, // PAL_DK       Working
        {0x86, 0x50, 0x4b}, // SECAM_L      ?
        {0x92, 0x30, 0x04}, // NTSC         ?
        //{0x92, 0x30, 0x40}, // NTSC_JP      ?
        //{0x8e, 0x0d, 0x77}, // FM_RADIO     ?
    },

    /*
    Add here new settings. But be careful:
    We can't use CardID's, there are more than one tables,
    BT8x8, SAA71xx and CX2388x. Please add an new entry in eTDA9887Card.
    For example:
    {
        TDA9887_NEW_CARD_SETTINGS,
        {0x00, 0x00, 0x00}, // PAL_BG       ?
        {0x00, 0x00, 0x00}, // PAL_I        ?
        {0x00, 0x00, 0x00}, // PAL_DK       ?
        {0x00, 0x00, 0x00}, // SECAM_L      ?
        {0x00, 0x00, 0x00}, // NTSC         ?
        {0x00, 0x00, 0x00}, // NTSC_JP      ?
        {0x00, 0x00, 0x00}, // FM_RADIO     ?
    },
    */

};

static void Tda9887_TunerSet( bool bPreSet, uint norm /* eVideoFormat videoFormat */ )
{
   BYTE tda9887set[5];

   dprintf2("Tda9887-TunerSet: preset=%d, norm=%d", bPreSet, norm);
   if (bPreSet)
   {
      if (Tda9887Type < TDA9887_LASTONE)
      {
         switch (norm)
         {
            case VIDEO_MODE_PAL:
               memcpy(&tda9887set[2], Tda9887TypeSettings[Tda9887Type].Pal_BG, 3);
               break;

            case VIDEO_MODE_SECAM:
               memcpy(&tda9887set[2], Tda9887TypeSettings[Tda9887Type].Secam_L, 3);
               break;

            case VIDEO_MODE_NTSC:
               memcpy(&tda9887set[2], Tda9887TypeSettings[Tda9887Type].Ntsc, 3);
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
      else
         debug1("Tda9887-TunerSet: invalid type %d", Tda9887Type);
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
#define TDA9887_VideoTrapBypassOFF     0x00    // bit b0
#define TDA9887_VideoTrapBypassON      0x01    // bit b0

#define TDA9887_AutoMuteFmInactive     0x00    // bit b1
#define TDA9887_AutoMuteFmActive       0x02    // bit b1

#define TDA9887_Intercarrier           0x00    // bit b2
#define TDA9887_QSS                    0x04    // bit b2

#define TDA9887_PositiveAmTV           0x00    // bit b3:4
#define TDA9887_FmRadio                0x08    // bit b3:4
#define TDA9887_NegativeFmTV           0x10    // bit b3:4

#define TDA9887_ForcedMuteAudioON      0x20    // bit b5
#define TDA9887_ForcedMuteAudioOFF     0x00    // bit b5

#define TDA9887_OutputPort1Active      0x00    // bit b6
#define TDA9887_OutputPort1Inactive    0x40    // bit b6
#define TDA9887_OutputPort2Active      0x00    // bit b7
#define TDA9887_OutputPort2Inactive    0x80    // bit b7


//// second reg
#define TDA9887_DeemphasisOFF          0x00    // bit c5
#define TDA9887_DeemphasisON           0x20    // bit c5
#define TDA9887_Deemphasis75           0x00    // bit c6
#define TDA9887_Deemphasis50           0x40    // bit c6
#define TDA9887_AudioGain0             0x00    // bit c7
#define TDA9887_AudioGain6             0x80    // bit c7


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

    if (haveTda9887Standard)
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

    if (haveTda9887Standard)
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

   if ((TunerDeviceI2C == 0) || (pTvCard == NULL) || (pTvCard->i2cBus == NULL))
   {
      debug0("Tuner-SetFrequency: called when not initialized");
      result = FALSE;
   }
   else if ((type < TUNERS_COUNT) && (type != TUNER_ABSENT))
   {
      dprintf4("Tuner-SetFrequency: type=%d, freq=%d (last=%d), norm=%d\n", type, wFrequency, lastWFreq, norm);

      I2CBus_Lock();

      if (haveTda9887Standard)
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

      if (haveTda9887Standard)
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
   uint i2cStart, i2cStop;
   BYTE i2cPort;
   uint defaultNorm = 0;
   eTDA9887Card IffType;
   bool result = FALSE;

   dprintf1("Tuner-Init: requested type %d\n", type);
   InitializeCriticalSection(&m_cCrit);
   TunerDeviceI2C = 0;
   pTvCard = NULL;

   if ((pNewTvCardIf != NULL) && (pNewTvCardIf->i2cBus != NULL))
   {
      pTvCard               = pNewTvCardIf;
      Tda9887Type           = TDA9887_LASTONE;
      haveTda9887Standard   = FALSE;
      haveTda9887Pinnacle   = FALSE;
      isTda9887PinnacleMono = FALSE;

      if (type < TUNERS_COUNT)
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

            // detect and initialize external IF demodulator (must be done before port scan)
            IffType = pTvCard->cfg->GetIffType(pTvCard, &haveTda9887Pinnacle, &isTda9887PinnacleMono);
            if ( haveTda9887Pinnacle && Tda9887_Detect(I2C_TDA9887_0) )
            {
               Tda9887Type = IffType;
               haveTda9887Pinnacle = TRUE;
               TDA9887Pinnacle_Init(TRUE, defaultNorm);
            }
            else if ( Tda9887_Detect(I2C_TDA9887_0) ||
                      Tda9887_Detect(I2C_TDA9887_1) )
            {
               Tda9887Type = IffType;
               haveTda9887Standard  = TRUE;
               Tda9887_Init(TRUE, defaultNorm);
            }
         }

         // scan the I2C bus for devices
         pTvCard->cfg->GetI2cScanRange(pTvCard, &i2cStart, &i2cStop);
         for (i2cPort = i2cStart; i2cPort <= i2cStop; i2cPort += 2)
         {
            dprintf1("Tuner-Init: check for tuner at 0x%02X\n", i2cPort);
            if ( pTvCard->i2cBus->I2cWrite(pTvCard, &i2cPort, 1) )
            {
               dprintf1("Tuner-Init: found tuner at 0x%02X\n", i2cPort);
               break;
            }
         }

         if (i2cPort <= i2cStop)
         {
            TunerDeviceI2C = i2cPort;

            if ( (type == TUNER_MT2032) || (type == TUNER_MT2032_PAL) ||
                 (type == TUNER_MT2050) || (type == TUNER_MT2050_PAL) )
            {
               Microtune_Initialize(type, defaultNorm);
            }
            result = TRUE;
         }
         else
         {
            TunerDeviceI2C = 0;
            MessageBox(NULL, "Warning: no tuner found on I2C bus\nin address range 0xc0 - 0xce", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
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

