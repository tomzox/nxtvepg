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
 *
 *  $Id: wintuner.c,v 1.2 2002/11/30 20:28:11 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_VBI
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "dsdrv/dsdrvlib.h"
#include "epgvbi/bt848.h"
#include "epgvbi/wintuner.h"


#define I2C_DELAY 0
#define I2C_TIMING (0x7<<4)
#define I2C_COMMAND (I2C_TIMING | BT848_I2C_SCL | BT848_I2C_SDA)


static BYTE TunerDeviceI2C;
static CRITICAL_SECTION m_cCrit;    //semaphore for I2C access

// ---------------------------------------------------------------------------
// Tuner table
//
struct TTunerType
{
   uchar  *name;
   uchar  vendor;
   uchar  type;

   double thresh1;       // frequency range for UHF, VHF-L, VHF_H
   double thresh2;
   uchar  VHF_L;
   uchar  VHF_H;
   uchar  UHF;
   uchar  config;
   ushort IFPCoff;
};

#define NOTUNER 0
#define PAL     1
#define PAL_I   2
#define NTSC    3
#define SECAM   4

#define NoTuner      0
#define Philips      1
#define TEMIC        2
#define Sony         3
#define Alps         4
#define LGINNOTEK    5
#define MICROTUNE    6
#define SHARP        7
#define Samsung      8
#define Microtune    9

// Tuner type table, copied from bttv tuner driver
static const struct TTunerType Tuners[TUNER_COUNT] =
{  // Note: order must be identical to enum in header file!
   { "none", NoTuner, NOTUNER,
     0,0,0x00,0x00,0x00,0x00,0x00},

   { "Temic PAL (4002 FH5)", TEMIC, PAL,
     16*140.25,16*463.25,0x02,0x04,0x01,0x8e,623},
   { "Philips PAL_I (FI1246 and compatibles)", Philips, PAL_I,
     16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,623},
   { "Philips NTSC (FI1236 and compatibles)", Philips, NTSC,
     16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,732},

   { "Philips (SECAM+PAL_BG) (FI1216MF, FM1216MF, FR1216MF)", Philips, SECAM,
     16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,623},
   { "Philips PAL_BG (FI1216 and compatibles)", Philips, PAL,
     16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,623},
   { "Temic NTSC (4032 FY5)", TEMIC, NTSC,
     16*157.25,16*463.25,0x02,0x04,0x01,0x8e,732},
   { "Temic PAL_I (4062 FY5)", TEMIC, PAL_I,
     16*170.00,16*450.00,0x02,0x04,0x01,0x8e,623},

   { "Temic NTSC (4036 FY5)", TEMIC, NTSC,
     16*157.25,16*463.25,0xa0,0x90,0x30,0x8e,732},
   { "Alps HSBH1", TEMIC, NTSC,
     16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
   { "Alps TSBE1",TEMIC,PAL,
     16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
   { "Alps TSBB5", Alps, PAL_I, /* tested (UK UHF) with Modulartech MM205 */
     16*133.25,16*351.25,0x01,0x02,0x08,0x8e,632},

   { "Alps TSBE5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
     16*133.25,16*351.25,0x01,0x02,0x08,0x8e,622},
   { "Alps TSBC5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
     16*133.25,16*351.25,0x01,0x02,0x08,0x8e,608},
   { "Temic PAL_BG (4006FH5)", TEMIC, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "Alps TSCH6",Alps,NTSC,
     16*137.25,16*385.25,0x14,0x12,0x11,0x8e,732},

   { "Temic PAL_DK (4016 FY5)",TEMIC,PAL,
     16*168.25,16*456.25,0xa0,0x90,0x30,0x8e,623},
   { "Philips NTSC_M (MK2)",Philips,NTSC,
     16*160.00,16*454.00,0xa0,0x90,0x30,0x8e,732},
   { "Temic PAL_I (4066 FY5)", TEMIC, PAL_I,
     16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
   { "Temic PAL* auto (4006 FN5)", TEMIC, PAL,
     16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},

   { "Temic PAL_BG (4009 FR5) or PAL_I (4069 FR5)", TEMIC, PAL,
     16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
   { "Temic NTSC (4039 FR5)", TEMIC, NTSC,
     16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
   { "Temic PAL/SECAM multi (4046 FM5)", TEMIC, PAL,
     16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
   { "Philips PAL_DK (FI1256 and compatibles)", Philips, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},

   { "Philips PAL/SECAM multi (FQ1216ME)", Philips, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG PAL_I+FM (TAPC-I001D)", LGINNOTEK, PAL_I,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG PAL_I (TAPC-I701D)", LGINNOTEK, PAL_I,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG NTSC+FM (TPI8NSR01F)", LGINNOTEK, NTSC,
     16*210.00,16*497.00,0xa0,0x90,0x30,0x8e,732},

   { "LG PAL_BG+FM (TPI8PSB01D)", LGINNOTEK, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "LG PAL_BG (TPI8PSB11D)", LGINNOTEK, PAL,
     16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
   { "Temic PAL* auto + FM (4009 FN5)", TEMIC, PAL,
     16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
   { "SHARP NTSC_JP (2U5JF5540)", SHARP, NTSC, /* 940=16*58.75 NTSC@Japan */
     16*137.25,16*317.25,0x01,0x02,0x08,0x8e,732 }, // Corrected to NTSC=732 (was:940)

   { "Samsung PAL TCPM9091PD27", Samsung, PAL,  /* from sourceforge v3tv */
     16*169,16*464,0xA0,0x90,0x30,0x8e,623},
   { "MT2032 universal", Microtune,PAL|NTSC,
     0,0,0,0,0,0,0},
   { "Temic PAL_BG (4106 FH5)", TEMIC, PAL,
     16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
   { "Temic PAL_DK/SECAM_L (4012 FY5)", TEMIC, PAL,
     16*140.25, 16*463.25, 0x02,0x04,0x01,0x8e,623},

   { "Temic NTSC (4136 FY5)", TEMIC, NTSC,
     16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
   { "LG PAL (newer TAPC series)", LGINNOTEK, PAL,
     16*170.00, 16*450.00, 0x01,0x02,0x08,0x8e,623},
   { "Philips PAL/SECAM multi (FM1216ME MK3)", Philips, PAL,
     16*160.00,16*442.00,0x01,0x02,0x04,0x8e,623 },
   { "LG NTSC (newer TAPC series)", LGINNOTEK, NTSC,
     16*170.00, 16*450.00, 0x01,0x02,0x08,0x8e,732},
};

#define TUNERS_COUNT (sizeof(Tuners) / sizeof(struct TTunerType))
//#if (TUNER_COUNT != TUNERS_COUNT)
//#error "tuner table length not equal enum"
//#endif

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
     standard           BG      DK      I       L       L`
     picture carrier    38.90   38.90   38.90   38.90   33.95
     colour             34.47   34.47   34.47   34.47   38.38
     sound 1            33.40   32.40   32.90   32.40   40.45
     sound 2            33.16   -       -       -       -
     NICAM              33.05   33.05   32.35   33.05   39.80
 */
#define PHILIPS_SET_PAL_I       0x01 /* Bit 2 always zero !*/
#define PHILIPS_SET_PAL_BGDK    0x09
#define PHILIPS_SET_PAL_L2      0x0a
#define PHILIPS_SET_PAL_L       0x0b    

/* system switching for Philips FI1216MF MK2
   from datasheet "1996 Jul 09",
    standard         BG     L      L'
    picture carrier  38.90  38.90  33.95
    colour           34.47  34.37  38.38
    sound 1          33.40  32.40  40.45
    sound 2          33.16  -      -
    NICAM            33.05  33.05  39.80
 */
#define PHILIPS_MF_SET_BG       0x01 /* Bit 2 must be zero, Bit 3 is system output */
#define PHILIPS_MF_SET_PAL_L    0x03 // France
#define PHILIPS_MF_SET_PAL_L2   0x02 // L'



// ---------------------------------------------------------------------------
// I2C bus access
// - does not use the hardware I2C capabilities of the Bt8x8
// - instead every bit of the protocol is implemented in sofware
//
static void
I2CBus_wait (int us)
{
   if (us > 0)
   {
      Sleep (us);
      return;
   }
   Sleep (0);
   Sleep (0);
   Sleep (0);
   Sleep (0);
   Sleep (0);
}

static void
I2C_SetLine (BOOL bCtrl, BOOL bData)
{
   BT8X8_WriteDword ( BT848_I2C, (bCtrl << 1) | bData);
   I2CBus_wait (I2C_DELAY);
}

static BOOL
I2C_GetLine ( void )
{
   return BT8X8_ReadDword (BT848_I2C) & 1;
}

#if 0  // unused code
static BYTE
I2C_Read (BYTE nAddr)
{
   DWORD i;
   volatile DWORD stat;

   // clear status bit ; BT848_INT_RACK is ro

   BT8X8_WriteDword (BT848_INT_STAT, BT848_INT_I2CDONE);
   BT8X8_WriteDword (BT848_I2C, (nAddr << 24) | I2C_COMMAND);

   for (i = 0x7fffffff; i; i--)
   {
      stat = BT8X8_ReadDword (BT848_INT_STAT);
      if (stat & BT848_INT_I2CDONE)
         break;
   }

   if (!i)
      return (BYTE) - 1;
   if (!(stat & BT848_INT_RACK))
      return (BYTE) - 2;

   return (BYTE) ((BT8X8_ReadDword (BT848_I2C) >> 8) & 0xFF);
}

static BOOL
I2C_Write (BYTE nAddr, BYTE nData1, BYTE nData2, BOOL bSendBoth)
{
   DWORD i;
   DWORD data;
   DWORD stat;

   /* clear status bit; BT848_INT_RACK is ro */
   BT8X8_WriteDword (BT848_INT_STAT, BT848_INT_I2CDONE);

   data = (nAddr << 24) | (nData1 << 16) | I2C_COMMAND;
   if (bSendBoth)
      data |= (nData2 << 8) | BT848_I2C_W3B;
   BT8X8_WriteDword (BT848_I2C, data);

   for (i = 0x7fffffff; i; i--)
   {
      stat = BT8X8_ReadDword (BT848_INT_STAT);
      if (stat & BT848_INT_I2CDONE)
         break;
   }

   if (!i)
      return FALSE;
   if (!(stat & BT848_INT_RACK))
      return FALSE;

   return TRUE;
}
#endif


static BOOL
I2CBus_Lock ( void )
{
   EnterCriticalSection (&m_cCrit);
   return TRUE;
}

static BOOL
I2CBus_Unlock ( void )
{
   LeaveCriticalSection (&m_cCrit);
   return TRUE;
}

static void
I2CBus_Start ( void )
{
   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   I2C_SetLine (1, 0);
   I2C_SetLine (0, 0);
}

static void
I2CBus_Stop ( void )
{
   I2C_SetLine (0, 0);
   I2C_SetLine (1, 0);
   I2C_SetLine (1, 1);
}

static void
I2CBus_One ( void )
{
   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   I2C_SetLine (0, 1);
}

static void
I2CBus_Zero ( void )
{
   I2C_SetLine (0, 0);
   I2C_SetLine (1, 0);
   I2C_SetLine (0, 0);
}

static BOOL
I2CBus_Ack ( void )
{
   BOOL bAck;

   I2C_SetLine (0, 1);
   I2C_SetLine (1, 1);
   bAck = !I2C_GetLine ();
   I2C_SetLine (0, 1);
   return bAck;
}

static BOOL
I2CBus_SendByte (BYTE nData, int nWaitForAck)
{
   I2C_SetLine (0, 0);
   nData & 0x80 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x40 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x20 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x10 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x08 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x04 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x02 ? I2CBus_One () : I2CBus_Zero ();
   nData & 0x01 ? I2CBus_One () : I2CBus_Zero ();
   if (nWaitForAck)
      I2CBus_wait (nWaitForAck);
   return I2CBus_Ack ();
}

static BYTE
I2CBus_ReadByte (BOOL bLast)
{
   int i;
   BYTE bData = 0;

   I2C_SetLine (0, 1);
   for (i = 7; i >= 0; i--)
   {
      I2C_SetLine (1, 1);
      if (I2C_GetLine ())
         bData |= (1 << i);
      I2C_SetLine (0, 1);
   }

   bLast ? I2CBus_One () : I2CBus_Zero ();
   return bData;
}

static BYTE
I2CBus_Read (BYTE nAddr)
{
   BYTE bData;

   I2CBus_Start ();
   I2CBus_SendByte (nAddr, 0);
   bData = I2CBus_ReadByte (TRUE);
   I2CBus_Stop ();
   return bData;
}

static BOOL
I2CBus_Write (BYTE nAddr, BYTE nData1, BYTE nData2, BOOL bSendBoth)
{
   BOOL bAck;

   I2CBus_Start ();
   I2CBus_SendByte (nAddr, 0);
   bAck = I2CBus_SendByte (nData1, 0);
   if (bSendBoth)
      bAck = I2CBus_SendByte (nData2, 0);
   I2CBus_Stop ();
   return bAck;
}


static BOOL
I2CBus_AddDevice (BYTE I2C_Port)
{
   BOOL bAck;

   // Test whether device exists
   I2CBus_Lock ();
   I2CBus_Start ();
   bAck = I2CBus_SendByte (I2C_Port, 0);
   I2CBus_Stop ();
   I2CBus_Unlock ();
   if (bAck)
      return TRUE;
   else
      return FALSE;
}

// ---------------------------------------------------------------------------
// The following code for MT2032 was taken verbatim from DScaler 4.00
// (but originates from bttv)
//

#define MT2032_OPTIMIZE_VCO 1   // perform VCO optimizations
static int  MT2032_XOGC = 4;    // holds the value of XOGC register after init
static bool MT2032_Initialized = FALSE; // MT2032 needs intialization?
static bool MT2032_Locked = FALSE;

static BYTE MT2032_GetRegister( BYTE regNum )
{
    I2CBus_Write(TunerDeviceI2C, regNum, 0, FALSE);
    return I2CBus_Read(TunerDeviceI2C);
}

static void MT2032_SetRegister( BYTE regNum, BYTE data )
{
    I2CBus_Write(TunerDeviceI2C, regNum, data, TRUE);
}

static bool MT2032_Initialize( void )
{
    BYTE rdbuf[22];
    int  xogc, xok = 0;
    int  i;

    //if (m_ExternalIFDemodulator != NULL)
    //{
    //    m_ExternalIFDemodulator->Init(TRUE, m_DefaultVideoFormat);
    //}

    for (i = 0; i < 21; i++)
    {
       rdbuf[i] = MT2032_GetRegister(i);
    }

    dprintf4("MT2032: Companycode=%02x%02x Part=%02x Revision=%02x\n", rdbuf[0x11],rdbuf[0x12],rdbuf[0x13],rdbuf[0x14]);

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


    //if (m_ExternalIFDemodulator != NULL)
    //{
    //    m_ExternalIFDemodulator->Init(FALSE, m_DefaultVideoFormat);
    //}
    
    MT2032_XOGC = xogc;
    MT2032_Initialized = TRUE;

    return TRUE;
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
    return lock;
}

static bool MT2032_SetIFFreq(int rfin, int if1, int if2, int from, int to, uint norm )
{
    uchar   buf[21];
    int     lint_try, sel, lock = 0;
    bool    result = FALSE;

    I2CBus_Lock();

    if ( MT2032_ComputeFreq(rfin, if1, if2, from, to, &buf[0], &sel, MT2032_XOGC) )
    {
        //if (m_ExternalIFDemodulator != NULL)
        //{
        //    m_ExternalIFDemodulator->TunerSet(TRUE, norm);
        //}

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

        //if (m_ExternalIFDemodulator != NULL)
        //{
        //    m_ExternalIFDemodulator->TunerSet(FALSE, norm);
        //}

        result = TRUE;
    }
    I2CBus_Unlock();

    return result;
}

// ---------------------------------------------------------------------------
// Set TV tuner synthesizer frequency
//
bool Tuner_SetFrequency( TUNER_TYPE type, uint wFrequency, uint norm )
{
   const struct TTunerType *pTun;
   double dFrequency;
   BYTE buffer[4];
   BYTE config;
   WORD div;
   bool bAck;

   static uint lastWFreq = 0;

   if ((type < TUNERS_COUNT) && (type != TUNER_NONE))
   {
      if (type == TUNER_MT2032)
      {
         return MT2032_SetIFFreq(wFrequency * 1000L / 16, 1090 * 1000 * 1000, 38900 * 1000, 32900 * 1000, 39900 * 1000, norm);
      }

      if ((wFrequency < 44*16) || (wFrequency > 958*16))
         debug1("Tuner-SetFrequency: freq %d/16 is out of range", wFrequency);

      pTun = Tuners + type;
      dFrequency = (double) wFrequency;

      if (dFrequency < pTun->thresh1)
         config = pTun->VHF_L;
      else if (dFrequency < pTun->thresh2)
         config = pTun->VHF_H;
      else
         config = pTun->UHF;

      // tv norm specification for multi-norm tuners
      switch (type)
      {
         case TUNER_PHILIPS_SECAM:
            /* XXX TODO: disabled until norm is provided by TV channel file parsers
            if (norm == VIDEO_MODE_SECAM)
               config |= 0x02;
            else
            */
               config &= ~0x02;
            break;
         case TUNER_TEMIC_4046FM5:
            config &= ~0x0f;
            /*
            if (norm == VIDEO_MODE_SECAM)
               config |= TEMIC_SET_PAL_L;
            else
            */
               config |= TEMIC_SET_PAL_BG;
            break;
         case TUNER_PHILIPS_FQ1216ME:
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

      div = (WORD)dFrequency + pTun->IFPCoff;

      if ((type == TUNER_PHILIPS_SECAM) && (wFrequency < lastWFreq))
      {
         /* Philips specification says to send config data before
         ** frequency in case (wanted frequency < current frequency) */
         buffer[0] = pTun->config;
         buffer[1] = config;
         buffer[2] = (div>>8) & 0x7f;
         buffer[3] = div      & 0xff;
      }
      else
      {
         buffer[0] = (div>>8) & 0x7f;
         buffer[1] = div      & 0xff;
         buffer[2] = pTun->config;
         buffer[3] = config;
      }
      lastWFreq = wFrequency;

      I2CBus_Lock ();              // Lock/wait

      if (!I2CBus_Write ((BYTE) TunerDeviceI2C, buffer[0], buffer[1], TRUE))
      {
         Sleep (1);
         if (!I2CBus_Write ((BYTE) TunerDeviceI2C, buffer[0], buffer[1], TRUE))
         {
            Sleep (1);
            if (!I2CBus_Write ((BYTE) TunerDeviceI2C, buffer[0], buffer[1], TRUE))
            {
               debug4("Tuner-SetFrequency: i2c write failed for word #1: type=%d, dev=0x%X, val1=0x%x, val2=0x%x", type, TunerDeviceI2C, buffer[0], buffer[1]);
               I2CBus_Unlock ();   // Unlock

               return (FALSE);
            }
         }
      }
      if (!(bAck = I2CBus_Write (TunerDeviceI2C, buffer[2], buffer[3], TRUE)))
      {
         Sleep (1);
         if (!(bAck = I2CBus_Write (TunerDeviceI2C, buffer[2], buffer[3], TRUE)))
         {
            Sleep (1);
            if (!(bAck = I2CBus_Write (TunerDeviceI2C, buffer[2], buffer[3], TRUE)))
            {
               debug4("Tuner-SetFrequency: i2c write failed for word #2: type=%d, dev=0x%X, val1=0x%x, val2=0x%x", type, TunerDeviceI2C, buffer[2], buffer[3]);
            }
         }
      }
      I2CBus_Unlock ();            // Unlock

      if (!bAck)
         return FALSE;
   }
   else if (type != TUNER_NONE)
      debug1("Tuner-SetFrequency: illegal tuner idx %d", type);

   return TRUE;
}

// ---------------------------------------------------------------------------
// Search a tuner in the list by given parameters
// - used to identify a manually configured tuner when importing a TV app INI file
//
uint Tuner_MatchByParams( uint thresh1, uint thresh2,
                          uchar VHF_L, uchar VHF_H, uchar UHF,
                          uchar config, ushort IFPCoff )
{
   const struct TTunerType * pTuner;
   uint idx;

   pTuner = Tuners;
   for (idx=0; idx < TUNERS_COUNT; idx++)
   {
      if ( (thresh1 == pTuner->thresh1) &&
           (thresh2 == pTuner->thresh2) &&
           (VHF_L == pTuner->VHF_L) &&
           (VHF_H == pTuner->VHF_H) &&
           (UHF == pTuner->UHF) &&
           (config == pTuner->config) &&
           (IFPCoff == pTuner->IFPCoff) )
      {
         return idx;
      }
      pTuner += 1;
   }
   return 0;
}

// ----------------------------------------------------------------------------
// Retrieve identifier strings for supported tuner types
// - called by user interface
//
const char * Tuner_GetName( uint idx )
{
   if (idx < TUNERS_COUNT)
      return Tuners[idx].name;
   else
      return NULL;
}

// ---------------------------------------------------------------------------
// Free module resources
//
void Tuner_Close( void )
{
   // nothing to do
}

// ---------------------------------------------------------------------------
// Auto-detect a tuner on the I2C bus
//
bool Tuner_Init( TUNER_TYPE type )
{
   uchar j;
   bool result = FALSE;

   if (type == TUNER_MT2032)
   {
      TunerDeviceI2C = 0xC0;
      MT2032_Initialize();
   }
   else if (type < TUNERS_COUNT)
   {
      InitializeCriticalSection (&m_cCrit);

      j = 0xc0;
      TunerDeviceI2C = j;

      while ((j <= 0xce) && (I2CBus_AddDevice ((BYTE) j) == FALSE))
      {
         j++;
         TunerDeviceI2C = j;
      }

      if (j <= 0xce)
      {
         result = TRUE;
      }
      else
         MessageBox(NULL, "Warning: no tuner found on Bt8x8 I2C bus\nIn address range 0xc0 - 0xce", "Nextview EPG", MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
   }
   else
      debug1("Init_Tuner: illegal tuner idx %d", type);

   return result;
}

