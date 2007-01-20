/////////////////////////////////////////////////////////////////////////////
// #Id: ParsingCommon.cpp,v 1.14 2005/07/17 15:59:22 to_see Exp #
/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2004 Atsushi Nakagawa.  All rights reserved.
/////////////////////////////////////////////////////////////////////////////
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
/////////////////////////////////////////////////////////////////////////////
// nxtvepg $Id: wintuner_typ.c,v 1.2 2006/01/05 14:22:36 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#define DEBUG_SWITCH DEBUG_SWITCH_DSDRV
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "dsdrv/tvcard.h"
#include "dsdrv/cfg_parse.h"
#include "dsdrv/wintuner.h"
#include "dsdrv/wintuner_typ.h"


static void SetTDA9887ModeMaskAndBits(TTDA9887Modes * specifics, BYTE bit, IN bool set);


//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

// It's not imperative that this list be updated when a new tuner
// is added but not updating it will mean the new tuner cannot be
// referred to by its name in the card list ini files.
const CParseConstant k_parseTunerConstants[] =
{
    PCINT( "AUTO",                         TUNER_AUTODETECT                ),
    PCINT( "SETUP",                        TUNER_USER_SETUP                ),
    PCINT( "ABSENT",                       TUNER_ABSENT                    ),
    PCINT( "PHILIPS_PAL_I",                TUNER_PHILIPS_PAL_I             ),
    PCINT( "PHILIPS_NTSC",                 TUNER_PHILIPS_NTSC              ),
    PCINT( "PHILIPS_SECAM",                TUNER_PHILIPS_SECAM             ),
    PCINT( "PHILIPS_PAL",                  TUNER_PHILIPS_PAL               ),
    PCINT( "TEMIC_4002FH5_PAL",            TUNER_TEMIC_4002FH5_PAL         ),
    PCINT( "TEMIC_4032FY5_NTSC",           TUNER_TEMIC_4032FY5_NTSC        ),
    PCINT( "TEMIC_4062FY5_PAL_I",          TUNER_TEMIC_4062FY5_PAL_I       ),
    PCINT( "TEMIC_4036FY5_NTSC",           TUNER_TEMIC_4036FY5_NTSC        ),
    PCINT( "ALPS_TSBH1_NTSC",              TUNER_ALPS_TSBH1_NTSC           ),
    PCINT( "ALPS_TSBE1_PAL",               TUNER_ALPS_TSBE1_PAL            ),
    PCINT( "ALPS_TSBB5_PAL_I",             TUNER_ALPS_TSBB5_PAL_I          ),
    PCINT( "ALPS_TSBE5_PAL",               TUNER_ALPS_TSBE5_PAL            ),
    PCINT( "ALPS_TSBC5_PAL",               TUNER_ALPS_TSBC5_PAL            ),
    PCINT( "TEMIC_4006FH5_PAL",            TUNER_TEMIC_4006FH5_PAL         ),
    PCINT( "PHILIPS_1236D_NTSC_INPUT1",    TUNER_PHILIPS_1236D_NTSC_INPUT1 ),
    PCINT( "PHILIPS_1236D_NTSC_INPUT2",    TUNER_PHILIPS_1236D_NTSC_INPUT2 ),
    PCINT( "ALPS_TSCH6_NTSC",              TUNER_ALPS_TSCH6_NTSC           ),
    PCINT( "TEMIC_4016FY5_PAL",            TUNER_TEMIC_4016FY5_PAL         ),
    PCINT( "PHILIPS_MK2_NTSC",             TUNER_PHILIPS_MK2_NTSC          ),
    PCINT( "TEMIC_4066FY5_PAL_I",          TUNER_TEMIC_4066FY5_PAL_I       ),
    PCINT( "TEMIC_4006FN5_PAL",            TUNER_TEMIC_4006FN5_PAL         ),
    PCINT( "TEMIC_4009FR5_PAL",            TUNER_TEMIC_4009FR5_PAL         ),
    PCINT( "TEMIC_4039FR5_NTSC",           TUNER_TEMIC_4039FR5_NTSC        ),
    PCINT( "TEMIC_4046FM5_MULTI",          TUNER_TEMIC_4046FM5_MULTI       ),
    PCINT( "PHILIPS_PAL_DK",               TUNER_PHILIPS_PAL_DK            ),
    PCINT( "PHILIPS_MULTI",                TUNER_PHILIPS_MULTI             ),
    PCINT( "LG_I001D_PAL_I",               TUNER_LG_I001D_PAL_I            ),
    PCINT( "LG_I701D_PAL_I",               TUNER_LG_I701D_PAL_I            ),
    PCINT( "LG_R01F_NTSC",                 TUNER_LG_R01F_NTSC              ),
    PCINT( "LG_B01D_PAL",                  TUNER_LG_B01D_PAL               ),
    PCINT( "LG_B11D_PAL",                  TUNER_LG_B11D_PAL               ),
    PCINT( "TEMIC_4009FN5_PAL",            TUNER_TEMIC_4009FN5_PAL         ),
    PCINT( "MT2032",                       TUNER_MT2032                    ),
    PCINT( "SHARP_2U5JF5540_NTSC",         TUNER_SHARP_2U5JF5540_NTSC      ),
    PCINT( "LG_TAPCH701P_NTSC",            TUNER_LG_TAPCH701P_NTSC         ),
    PCINT( "SAMSUNG_PAL_TCPM9091PD27",     TUNER_SAMSUNG_PAL_TCPM9091PD27  ),
    PCINT( "TEMIC_4106FH5",                TUNER_TEMIC_4106FH5             ),
    PCINT( "TEMIC_4012FY5",                TUNER_TEMIC_4012FY5             ),
    PCINT( "TEMIC_4136FY5",                TUNER_TEMIC_4136FY5             ),
    PCINT( "LG_TAPCNEW_PAL",               TUNER_LG_TAPCNEW_PAL            ),
    PCINT( "PHILIPS_FM1216ME_MK3",         TUNER_PHILIPS_FM1216ME_MK3      ),
    PCINT( "LG_TAPCNEW_NTSC",              TUNER_LG_TAPCNEW_NTSC           ),
    PCINT( "MT2032_PAL",                   TUNER_MT2032_PAL                ),
    PCINT( "PHILIPS_FI1286_NTSC_M_J",      TUNER_PHILIPS_FI1286_NTSC_M_J   ),
    PCINT( "MT2050",                       TUNER_MT2050                    ),
    PCINT( "MT2050_PAL",                   TUNER_MT2050_PAL                ),
    PCINT( "PHILIPS_4IN1",                 TUNER_PHILIPS_4IN1              ),
    PCINT( "TCL_2002N",                    TUNER_TCL_2002N                 ),
    PCINT( "HITACHI_NTSC",                 TUNER_HITACHI_NTSC              ),
    PCINT( "PHILIPS_PAL_MK",               TUNER_PHILIPS_PAL_MK            ),
    PCINT( "PHILIPS_FM1236_MK3",           TUNER_PHILIPS_FM1236_MK3        ),
    PCINT( "LG_NTSC_TAPE",                 TUNER_LG_NTSC_TAPE              ),
    PCINT( "TNF_8831BGFF",                 TUNER_TNF_8831BGFF              ),
    PCINT( "PHILIPS_FM1256_IH3",           TUNER_PHILIPS_FM1256_IH3        ),
    PCINT( "PHILIPS_FQ1286",               TUNER_PHILIPS_FQ1286            ),
    PCINT( "LG_PAL_TAPE",                  TUNER_LG_PAL_TAPE               ),
    PCINT( "TUNER_PHILIPS_FQ1216AME_MK4",  TUNER_PHILIPS_FQ1216AME_MK4     ),
    PCINT( "PHILIPS_FQ1236A_MK4",          TUNER_PHILIPS_FQ1236A_MK4       ),
    PCINT( "PHILIPS_TDA8275",              TUNER_TDA8275                   ),
    PCINT( "YMEC_TVF_8531MF",              TUNER_YMEC_TVF_8531MF           ),
    PCINT( "YMEC_TVF_5533MF",              TUNER_YMEC_TVF_5533MF           ),
    PCINT( "TENA_9533_DI",                 TUNER_TENA_9533_DI              ),
    PCINT( "PHILIPS_FMD1216ME_MK3",        TUNER_PHILIPS_FMD1216ME_MK3     ),
    PCEND
};

const CParseConstant k_parseTDAFormatConstants[] =
{
    PCINT( "GLOBAL",                       TDA9887_FORMAT_NONE         ),
    PCINT( "PAL-BG",                       TDA9887_FORMAT_PAL_BG       ),
    PCINT( "PAL-I",                        TDA9887_FORMAT_PAL_I        ),
    PCINT( "PAL-DK",                       TDA9887_FORMAT_PAL_DK       ),
    PCINT( "PAL-MN",                       TDA9887_FORMAT_PAL_MN       ),
    PCINT( "SECAM-L",                      TDA9887_FORMAT_SECAM_L      ),
    PCINT( "SECAM-DK",                     TDA9887_FORMAT_SECAM_DK     ),
    PCINT( "NTSC-M",                       TDA9887_FORMAT_NTSC_M       ),
    PCINT( "NTSC-JP",                      TDA9887_FORMAT_NTSC_JP      ),
    PCINT( "Radio",                        TDA9887_FORMAT_RADIO        ),
    PCEND
};

const CParseConstant k_parseCarrierConstants[] =
{
    PCINT( "intercarrier",             0 ),
    PCINT( "qss",                      1 ),
    PCEND
};

const CParseConstant k_parseYesNoConstants[] =
{
    PCINT( "yes",                      1 ),
    PCINT( "no",                       0 ),
    PCINT( "true",                     1 ),
    PCINT( "false",                    0 ),
    PCINT( "active",                   1 ),
    PCINT( "inactive",                 0 ),
    PCINT( "1",                        1 ),
    PCINT( "0",                        0 ),
    PCEND
};

const CParseConstant k_parseTakeoverPointConstants[] =
{
    PCINT( "min",                      TDA9887_SM_TAKEOVERPOINT_MIN        ),
    PCINT( "max",                      TDA9887_SM_TAKEOVERPOINT_MAX        ),
    PCINT( "default",                  TDA9887_SM_TAKEOVERPOINT_DEFAULT    ),
    PCEND
};

const CParseTag k_parseUseTDA9887SetOverride[] =
{
    PTCONST( "Format",           PARSE_CONSTANT,     0, 8,   k_parseTDAFormatConstants,      PASS_TO_PARENT  ),
    PTLEAF(  "Intercarrier",     0,                  0, 0,   NULL,                           PASS_TO_PARENT  ),
    PTLEAF(  "QSS",              0,                  0, 0,   NULL,                           PASS_TO_PARENT  ),
    PTCONST( "Carrier",          PARSE_CONSTANT,     0, 16,  k_parseCarrierConstants,        PASS_TO_PARENT  ),
    PTCONST( "OutputPort1",      PARSE_CONSTANT,     0, 8,   k_parseYesNoConstants,          PASS_TO_PARENT  ),
    PTCONST( "OutputPort2",      PARSE_CONSTANT,     0, 8,   k_parseYesNoConstants,          PASS_TO_PARENT  ),
    PTCONST( "TakeOverPoint",    PARSE_NUM_OR_CONST, 0, 8,   k_parseTakeoverPointConstants,  PASS_TO_PARENT  ),
    PTEND
};

const CParseTag k_parseUseTDA9887[] =
{
    PTCONST( "Use",              PARSE_CONSTANT,     0, 8,   k_parseYesNoConstants,          PASS_TO_PARENT  ),
    PTCHILD( "SetModes",         PARSE_CHILDREN,     0, 9,   k_parseUseTDA9887SetOverride,   PASS_TO_PARENT  ),
    PTEND
};


//////////////////////////////////////////////////////////////////////////
// Interpreters
//////////////////////////////////////////////////////////////////////////

BOOL ReadTunerProc(IN int report, IN const CParseTag* tag, IN unsigned char type,
                   IN const CParseValue* value, IN OUT TParseTunerInfo* tunerInfo, char ** ppErrMsg)
{
    if (report == REPORT_OPEN)
    {
        // Set the default tuner id for if there is no value.
        tunerInfo->tunerId = TUNER_ABSENT;
    }
    else if (report == REPORT_VALUE)
    {
        int n = CParseValue_GetNumber(value);
        if (n < TUNER_AUTODETECT || n >= TUNER_LASTONE)
        {
            *ppErrMsg = ("Invalid tuner Id");
            return FALSE;
        }
        tunerInfo->tunerId = (eTunerId)(n);
    }
    else if (report == REPORT_CLOSE)
    {
        // The value could be considered read at the end of REPORT_VALUE
        // but putting the 'return TRUE' here is also good.
        return TRUE;
    }
    return FALSE;
}


BOOL ReadUseTDA9887Proc(IN int report, IN const CParseTag* tag, IN unsigned char type,
                        IN const CParseValue* value, IN OUT TParseUseTDA9887Info* useTDA9887Info, char ** ppErrMsg)
{
    // Use
    if (tag == k_parseUseTDA9887 + 0)
    {
        if (report == REPORT_VALUE)
        {
            useTDA9887Info->useTDA9887 = CParseValue_GetNumber(value) != 0;
        }
    }
    // SetOverride
    else if (tag == k_parseUseTDA9887 + 1)
    {
        if (report == REPORT_OPEN)
        {
            useTDA9887Info->_readingFormat = TDA9887_FORMAT_NONE;
            useTDA9887Info->_readingModes.mask = 0;
            useTDA9887Info->_readingModes.bits = 0;
        }
        else if (report == REPORT_CLOSE)
        {
            // This should not fail because constants only provide 0 .. TDA9887_LASTFORMAT.
            assert(useTDA9887Info->_readingFormat == TDA9887_FORMAT_NONE ||
                (useTDA9887Info->_readingFormat >= 0 &&
                useTDA9887Info->_readingFormat < TDA9887_FORMAT_LASTONE));

            // It is pointless copying if mask is zero.
            if (useTDA9887Info->_readingModes.mask != 0)
            {
                if (useTDA9887Info->_readingFormat == TDA9887_FORMAT_NONE)
                {
                    int i;
                    // Copy for all formats.
                    for (i = 0; i < TDA9887_FORMAT_LASTONE; i++)
                    {
                        useTDA9887Info->tdaModes[i].bits &= ~useTDA9887Info->_readingModes.mask;
                        useTDA9887Info->tdaModes[i].bits |= useTDA9887Info->_readingModes.bits;
                        useTDA9887Info->tdaModes[i].mask |= useTDA9887Info->_readingModes.mask;
                    }
                }
                else
                {
                    int i = useTDA9887Info->_readingFormat;
                    useTDA9887Info->tdaModes[i].bits &= ~useTDA9887Info->_readingModes.mask;
                    useTDA9887Info->tdaModes[i].bits |= useTDA9887Info->_readingModes.bits;
                    useTDA9887Info->tdaModes[i].mask |= useTDA9887Info->_readingModes.mask;
                }
            }
        }
    }
    // Format
    else if (tag == k_parseUseTDA9887SetOverride + 0)
    {
        if (report == REPORT_VALUE)
        {
            useTDA9887Info->_readingFormat = (eTDA9887Format)(CParseValue_GetNumber(value));
        }
    }
    // Intercarrier
    else if (tag == k_parseUseTDA9887SetOverride + 1)
    {
        if (report == REPORT_TAG)
        {
            SetTDA9887ModeMaskAndBits(&useTDA9887Info->_readingModes,
                TDA9887_SM_CARRIER_QSS, FALSE);
        }
    }
    // QSS
    else if (tag == k_parseUseTDA9887SetOverride + 2)
    {
        if (report == REPORT_TAG)
        {
            SetTDA9887ModeMaskAndBits(&useTDA9887Info->_readingModes,
                TDA9887_SM_CARRIER_QSS, TRUE);
        }
    }
    // Carrier
    else if (tag == k_parseUseTDA9887SetOverride + 3)
    {
        if (report == REPORT_VALUE)
        {
            SetTDA9887ModeMaskAndBits(&useTDA9887Info->_readingModes,
                TDA9887_SM_CARRIER_QSS, CParseValue_GetNumber(value) != 0);
        }
    }
    // OutputPort1
    else if (tag == k_parseUseTDA9887SetOverride + 4)
    {
        if (report == REPORT_VALUE)
        {
            SetTDA9887ModeMaskAndBits(&useTDA9887Info->_readingModes,
                TDA9887_SM_OUTPUTPORT1_INACTIVE, CParseValue_GetNumber(value) == 0);
        }
    }
    // OutputPort2
    else if (tag == k_parseUseTDA9887SetOverride + 5)
    {
        if (report == REPORT_VALUE)
        {
            SetTDA9887ModeMaskAndBits(&useTDA9887Info->_readingModes,
                TDA9887_SM_OUTPUTPORT2_INACTIVE, CParseValue_GetNumber(value) == 0);
        }
    }
    // TakeOverPoint
    else if (tag == k_parseUseTDA9887SetOverride + 6)
    {
        if (report == REPORT_VALUE)
        {
            BYTE point = (BYTE)(CParseValue_GetNumber(value));
            if (point & ~TDA9887_SM_TAKEOVERPOINT_MASK)
            {
                *ppErrMsg = ("Invalid value of TakeOverPoint");
                return FALSE;
            }

            useTDA9887Info->_readingModes.mask |= TDA9887_SM_TAKEOVERPOINT_MASK;
            useTDA9887Info->_readingModes.bits &= ~TDA9887_SM_TAKEOVERPOINT_MASK;
            useTDA9887Info->_readingModes.bits |= (point << TDA9887_SM_TAKEOVERPOINT_OFFSET);
        }
    }
    // This unknown tag is hopefully the parent tag.  There's no other way of
    // checking.  An incorrect tag here can only result from incorrect setup
    // outside ParsingCommon's control.
    else
    {
        if (report == REPORT_OPEN)
        {
            useTDA9887Info->useTDA9887 = TRUE;
            ZeroMemory(useTDA9887Info->tdaModes, sizeof(useTDA9887Info->tdaModes));
        }
        else if (report == REPORT_CLOSE)
        {
            // Everything about the UseTDA9887 tag is considered read and
            // ParseUseTDA9887Info considered fill at this point.
            return TRUE;
        }
    }
    return FALSE;
}

static void SetTDA9887ModeMaskAndBits(TTDA9887Modes * specifics, BYTE bit, IN bool set)
{
    specifics->mask |= bit;
    if (set)
    {
        specifics->bits |= bit;
    }
    else
    {
        specifics->bits &= ~bit;
    }
}


