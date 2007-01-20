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
 *  DScaler #Id: SAA7134Card_Types.cpp,v 1.58 2004/12/16 04:53:51 atnak Exp #
 *
 *  $Id: saa7134_typ.c,v 1.24 2006/10/14 15:48:47 tom Exp tom $
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
#include "dsdrv/debuglog.h"
#include "dsdrv/tvcard.h"
#include "dsdrv/cfg_parse.h"
#include "dsdrv/wintuner.h"
#include "dsdrv/wintuner_typ.h"
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
    INPUTTYPE_FINAL,
} eInputType;

/// SAA7134's video input pins
/// RegSpy: SAA7134_ANALOG_IN_CTRL1
/// RegSpy: 0000nnnn
/// RegSpy: nnnn: 5 = reserved
/// RegSpy: nnnn: 6 or 8 = 0 + s-video
/// RegSpy: nnnn: 7 or 9 = 1 + s-video
typedef enum
{
    VIDEOINPUTSOURCE_NONE = -1,     // reserved for radio
    VIDEOINPUTSOURCE_PIN0 = 0,
    VIDEOINPUTSOURCE_PIN1,
    VIDEOINPUTSOURCE_PIN2,
    VIDEOINPUTSOURCE_PIN3,
    VIDEOINPUTSOURCE_PIN4,
} eVideoInputSource;

/// Possible clock crystals a card could have
typedef enum
{
    AUDIOCRYSTAL_NONE = 0,          // only on saa7130
    AUDIOCRYSTAL_32110kHz,          // 0x187DE7
    AUDIOCRYSTAL_24576kHz,          // 0x200000
} eAudioCrystal;

typedef enum
{
    /// reserved, used in INPUTTYPE_FINAL
    AUDIOINPUTSOURCE_NONE           = -1,
    /// auxiliary line 1 input - 0x00
    AUDIOINPUTSOURCE_LINE1          = 0,
    /// auxiliary line 2 input - 0x01
    AUDIOINPUTSOURCE_LINE2          = 1,
    /// standard tuner via dac - 0x02
    AUDIOINPUTSOURCE_DAC            = 2,

    /// These two are for SAA7133/5
    /// auxiliary line 1 via dac - 0x02
    AUDIOINPUTSOURCE_DAC_LINE1      = 3,
    /// auxiliary line 2 via dac - 0x02
    AUDIOINPUTSOURCE_DAC_LINE2      = 4,
} eAudioInputSource;

/// Defines each input on a card
typedef struct
{
    /// Name of the input
    char szName[64];
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
#define SA_INPUTS_PER_CARD 7
#define SA_AUTODETECT_ID_PER_CARD 3
typedef struct TCardType_struct
{
    struct TCardType_struct * pNext;

    char szName[128];
    WORD DeviceId;
    int NumInputs;
    TInputType Inputs[SA_INPUTS_PER_CARD];
    eTunerId TunerId;
    /// The type of clock crystal the card has
    eAudioCrystal AudioCrystal;
    DWORD dwGPIOMode;
    DWORD AutoDetectId[SA_AUTODETECT_ID_PER_CARD];
    BOOL bUseTDA9887;
    // CCardTypeEx //TZO++
    TTDA9887FormatModes tda9887Modes[TDA9887_FORMAT_LASTONE];
} TCardType;

/// used to store temporary information used while parsing
/// the SAA713x card list
typedef struct TParseCardInfo_struct
{
    // List of all cards parsed and last item for currently parsing.
    TCardType*                  pCardList;
    // Pointer to the last item in pCardList when a card is being parsed.
    TCardType*                  pCurrentCard;
    // Number of cards successfully parsed so far.
    size_t                      nGoodCards;
    // Pointer to the HCParser instance doing the parsing.
    //HCParser::CHCParser*        pHCParser;
    // Used by ParsingCommon's tuner parser for temporary data.
    TParseTunerInfo             tunerInfo;
    // Used by ParsingCommon's useTDA9887 parser for temporary data.
    TParseUseTDA9887Info        useTDA9887Info;
} TParseCardInfo;

static bool ReadCardInputInfoProc(int report, const CParseTag* tag, unsigned char,
                                  const CParseValue* value, void* context, char ** ppErrMsg);
static bool ReadCardInputProc(int report, const CParseTag* tag, unsigned char,
                              const CParseValue*, void* context, char ** ppErrMsg);
static bool ReadCardUseTDA9887Proc(int report, const CParseTag* tag, unsigned char type,
                                   const CParseValue* value, void* context, char ** ppErrMsg);
static bool ReadCardDefaultTunerProc(int report, const CParseTag* tag, unsigned char type,
                                     const CParseValue* value, void* context, char ** ppErrMsg);
static bool ReadCardInfoProc(int report, const CParseTag* tag, unsigned char,
                             const CParseValue* value, void* context, char ** ppErrMsg);
static bool ReadCardAutoDetectIDProc(int report, const CParseTag* tag, unsigned char,
                                     const CParseValue* value, void* context, char ** ppErrMsg);
static bool ReadCardProc(int report, const CParseTag*, unsigned char dummy1,
                         const CParseValue* dummy2, void* context, char ** ppErrMsg);
static BOOL InitializeSAA713xCardList( void );
static void InitializeSAA713xUnknownCard( void );
static void StandardSAA7134InputSelect(TVCARD * pTvCard, int nInput);

static const char* const k_SAA713xCardListFilename = "SAA713xCards.ini";

#define SAA7134CARDID_UNKNOWN 0
static const TCardType m_SAA7134UnknownCard =
// SAA7134CARDID_UNKNOWN - Unknown Card
{
    NULL,
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
    AUDIOCRYSTAL_32110kHz,
    0,
};

static const TCardType m_SAA7134CardDefaults =
{
    NULL, "", 0x0000, 0, { }, TUNER_ABSENT, AUDIOCRYSTAL_NONE, 0,
    { 0, 0, 0 }, FALSE
};

static TCardType * m_SAA713xCards = NULL;

#define INIT_CARD_LIST() do{if (m_SAA713xCards == NULL) {InitializeSAA713xCardList();}}while(0)

//////////////////////////////////////////////////////////////////////////
// SAA713x card list parsing constants
//////////////////////////////////////////////////////////////////////////

static const CParseConstant k_parseAudioPinConstants[] =
{
    PCINT( "NONE",         AUDIOINPUTSOURCE_NONE   ),
    PCINT( "LINE1",        AUDIOINPUTSOURCE_LINE1  ),
    PCINT( "LINE2",        AUDIOINPUTSOURCE_LINE2  ),
    PCINT( "DAC",          AUDIOINPUTSOURCE_DAC    ),
    PCEND
};

static const CParseConstant k_parseInputTypeConstants[] =
{
    PCINT( "COMPOSITE",    INPUTTYPE_COMPOSITE     ),
    PCINT( "SVIDEO",       INPUTTYPE_SVIDEO        ),
    PCINT( "TUNER",        INPUTTYPE_TUNER         ),
    PCINT( "CCIR",         INPUTTYPE_CCIR          ),
    PCINT( "RADIO",        INPUTTYPE_RADIO         ),
// MUTE input was defined but not currently used by CSAA7134Card.
//  PCINT( "MUTE",         INPUTTYPE_MUTE          ),
// FINAL isn't necessary because Final() does the same thing.
//  PCINT( "FINAL",        INPUTTYPE_FINAL     ),
    PCEND
};

static const CParseConstant k_parseAudioCrystalConstants[] =
{
    PCINT( "NONE",         AUDIOCRYSTAL_NONE       ),
    PCINT( "32MHz",        AUDIOCRYSTAL_32110kHz   ),
    PCINT( "24MHz",        AUDIOCRYSTAL_24576kHz   ),
    PCEND
};

//////////////////////////////////////////////////////////////////////////
// SAA713x card list parsing values
//////////////////////////////////////////////////////////////////////////

static const CParseTag  k_parseInputGPIOSet[]   =
{
    PTLEAF(  "Bits",             PARSE_NUMERIC,      1,  16, NULL,                           ReadCardInputInfoProc       ),
    PTLEAF(  "Mask",             PARSE_NUMERIC,      1,  16, NULL,                           ReadCardInputInfoProc       ),
    PTEND
};

static const CParseTag  k_parseCardInput[] =
{
    PTLEAF(  "Name",             PARSE_STRING,       1, 63,  NULL,                          ReadCardInputInfoProc       ),
    PTCONST( "Type",             PARSE_CONSTANT,     1, 16,  k_parseInputTypeConstants,     ReadCardInputInfoProc       ),
    PTLEAF(  "VideoPin",         PARSE_NUMERIC,      0, 8,   NULL,                          ReadCardInputInfoProc       ),
    PTCONST( "AudioPin",         PARSE_NUM_OR_CONST, 0, 8,   k_parseAudioPinConstants,      ReadCardInputInfoProc       ),
    PTCHILD( "GPIOSet",          PARSE_CHILDREN,     0, 1,   k_parseInputGPIOSet,           NULL                        ),
    PTEND
};

static const CParseTag  k_parseAutoDetectID[]   =
{
    PTLEAF(  "0",                PARSE_NUMERIC,      0, 16,  NULL,                           ReadCardAutoDetectIDProc    ),
    PTLEAF(  "1",                PARSE_NUMERIC,      0, 16,  NULL,                           ReadCardAutoDetectIDProc    ),
    PTLEAF(  "2",                PARSE_NUMERIC,      0, 16,  NULL,                           ReadCardAutoDetectIDProc    ),
    PTEND
};

static const CParseTag  k_parseCard[]   =
{
    PTLEAF(  "Name",             PARSE_STRING,       1, 127, NULL,                           ReadCardInfoProc            ), 
    PTLEAF(  "DeviceID",         PARSE_NUMERIC,      1, 8,   NULL,                           ReadCardInfoProc            ),
    PTCONST( "DefaultTuner",     PARSE_NUM_OR_CONST, 0, 32,  k_parseTunerConstants,          ReadCardDefaultTunerProc    ),
    PTCONST( "AudioCrystal",     PARSE_CONSTANT,     0, 8,   k_parseAudioCrystalConstants,   ReadCardInfoProc            ),
    PTLEAF(  "GPIOMask",         PARSE_NUMERIC,      0, 16,  NULL,                           ReadCardInfoProc            ),
    PTCHILD( "AutoDetectID",     PARSE_CHILDREN,     0, 1,   k_parseAutoDetectID,            NULL                        ),
    PTCHILD( "Input",            PARSE_CHILDREN,     0, 7,   k_parseCardInput,               ReadCardInputProc           ),
    PTCHILD( "Final",            PARSE_CHILDREN,     0, 7,   k_parseCardInput+2,             ReadCardInputProc           ),
    PTCHILD( "UseTDA9887",       PARSE_CHILDREN,     0, 1,   k_parseUseTDA9887,              ReadCardUseTDA9887Proc      ),
    PTEND
};

static const CParseTag  k_parseCardList[]   =
{
    PTCHILD( "Card",             PARSE_CHILDREN,     0, 512, k_parseCard,                    ReadCardProc                ),
    PTEND
};

// ------------------------------------------------------------------------------
// Card list implementation
//
static void CList_Destroy( TCardType ** ppList )
{
    TCardType * pWalk;
    TCardType * pNext;

    pWalk = *ppList;
    while (pWalk != NULL)
    {
        pNext = pWalk->pNext;
        xfree(pWalk);
        pWalk = pNext;
    }
    *ppList = NULL;
}

static TCardType * CList_GetFront( TCardType * pList )
{
    return pList;
}

static TCardType * CList_GetNext( TCardType * pList )
{
    if (pList != NULL)
        return pList->pNext;
    else
        return NULL;
}

static TCardType * CList_Get( TCardType * pList, uint idx )
{
    while ((pList != NULL) && (idx != 0))
    {
        idx -= 1;
        pList = pList->pNext;
    }
    return pList;
}

static uint CList_GetSize( TCardType * pList )
{
    uint size = 0;

    while (pList != NULL)
    {
        size += 1;
        pList = pList->pNext;
    }
    return size;
}

static TCardType * CList_PushBack( TCardType ** ppList, const TCardType * pElem )
{
    TCardType * pWalk;
    TCardType * pCopy = xmalloc(sizeof(TCardType));

    *pCopy = *pElem;
    pCopy->pNext = NULL;

    if (*ppList != NULL)
    {
        pWalk = *ppList;
        while (pWalk->pNext != NULL)
        {
            pWalk = pWalk->pNext;
        }
        pWalk->pNext = pCopy;
    }
    else
        *ppList = pCopy;

    return pCopy;
}

static void CList_Resize( TCardType ** ppList, uint count )
{
    TCardType * pWalk = *ppList;
    TCardType * pPrev = NULL;
    uint size = 0;

    while ((pWalk != NULL) && (size < count))
    {
        size += 1;
        pPrev = pWalk;
        pWalk = pWalk->pNext;
    }

    if (pPrev != NULL)
    {
        CList_Destroy(&pPrev->pNext);
    }
    else
        CList_Destroy(ppList);
}

// ------------------------------------------------------------------------------

static BOOL InitializeSAA713xCardList( void )
{
    TParseCardInfo parseInfo;
    BOOL result = TRUE;

    InitializeSAA713xUnknownCard();

    HCParser_Init(k_parseCardList);

    parseInfo.pCardList = m_SAA713xCards;
    parseInfo.pCurrentCard = NULL;
    parseInfo.nGoodCards = 1;

    // No need to use ParseLocalFile() because DScaler does SetExeDirectory()
    // at the beginning.
    while (!HCParser_ParseFile(k_SAA713xCardListFilename, (void*)&parseInfo, FALSE))
    {
        DWORD iRetVal;
        char * pMsg = HCParser_GetError();

        if (pMsg != NULL)
           iRetVal = MessageBox(NULL, pMsg, k_SAA713xCardListFilename, MB_ICONSTOP | MB_ABORTRETRYIGNORE | MB_TASKMODAL | MB_SETFOREGROUND);
        else
           iRetVal = MessageBox(NULL, "Failed to read card definition file.", k_SAA713xCardListFilename, MB_ICONSTOP | MB_ABORTRETRYIGNORE | MB_TASKMODAL | MB_SETFOREGROUND);

        // retry parsing; discard all entries except for the "unknown" type
        if (iRetVal == IDABORT)
        {
            result = FALSE;
            break;
        }

        parseInfo.pCurrentCard = NULL;
        parseInfo.nGoodCards = 1;
        CList_Resize(&parseInfo.pCardList, 1);
    }

    if (result)
    {
       // These can be one extra card in the list than the number of nGoodCards.
       if (parseInfo.nGoodCards < CList_GetSize(parseInfo.pCardList))
       {
           CList_Resize(&parseInfo.pCardList, parseInfo.nGoodCards);
       }

       LOG(1, "SAA713x cardlist: %lu card(s) read", parseInfo.nGoodCards);
    }

    HCParser_Destroy();

    return result;
}


static void InitializeSAA713xUnknownCard( void )
{
    if (m_SAA713xCards == NULL)
    {
        CList_PushBack(&m_SAA713xCards, &m_SAA7134UnknownCard);
    }
}


static bool ReadCardInputInfoProc(int report, const CParseTag* tag, unsigned char dummy1,
                                  const CParseValue* value, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;

    TInputType* input;

    if (report != REPORT_VALUE)
    {
        return TRUE;
    }

    input = &parseInfo->pCurrentCard->Inputs[parseInfo->pCurrentCard->NumInputs];

    // Name
    if (tag == k_parseCardInput + 0)
    {
        if (CParseValue_GetString(value) == '\0')
        {
            *ppErrMsg = ("\"\" is not a valid name of an input");
            return FALSE;
        }
        strncpy(input->szName, CParseValue_GetString(value), sizeof(input->szName) - 1);
    }
    // Type
    else if (tag == k_parseCardInput + 1)
    {
        input->InputType = (eInputType)CParseValue_GetNumber(value);
    }
    // VideoPin
    else if (tag == k_parseCardInput + 2)
    {
        int n = CParseValue_GetNumber(value);
        if (n < VIDEOINPUTSOURCE_NONE || n > VIDEOINPUTSOURCE_PIN4)
        {
            *ppErrMsg = ("VideoPin must be between -1 and 4");
            return FALSE;
        }
        input->VideoInputPin = (eVideoInputSource) n;
    }
    // AudioPin
    else if (tag == k_parseCardInput + 3)
    {
        int n = CParseValue_GetNumber(value);
        if (n < AUDIOINPUTSOURCE_NONE || n > AUDIOINPUTSOURCE_DAC)
        {
            *ppErrMsg = ("AudioPin must be between -1 and 2");
            return FALSE;
        }
        input->AudioLineSelect = (eAudioInputSource)(n);
    }
    // GPIOSet->Bits
    else if (tag == k_parseInputGPIOSet + 0)
    {
        input->dwGPIOStatusBits = (DWORD)CParseValue_GetNumber(value);
    }
    // GPIOSet->Mask
    else if (tag == k_parseInputGPIOSet + 1)
    {
        input->dwGPIOStatusMask = (DWORD)CParseValue_GetNumber(value);
    }
    return TRUE;
}


static bool ReadCardInputProc(int report, const CParseTag* tag, unsigned char dummy1,
                              const CParseValue* dummy2, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;

    switch (report)
    {
    case REPORT_OPEN:
        {
            TInputType* input = &parseInfo->pCurrentCard->Inputs[parseInfo->pCurrentCard->NumInputs];
            input->szName[0] = '\0';
            input->InputType = (tag == k_parseCard + 7) ? INPUTTYPE_FINAL : INPUTTYPE_COMPOSITE;
            input->VideoInputPin = VIDEOINPUTSOURCE_NONE;
            input->AudioLineSelect = AUDIOINPUTSOURCE_NONE;
            input->dwGPIOStatusMask = 0;
            input->dwGPIOStatusBits = 0;
        }
        break;
    case REPORT_CLOSE:
        parseInfo->pCurrentCard->NumInputs++;
        break;
    }
    return TRUE;
}

static bool ReadCardUseTDA9887Proc(int report, const CParseTag* tag, unsigned char type,
                                   const CParseValue* value, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;

    // Return TRUE means parseInfo->useTDA9887Info is ready.
    if (ReadUseTDA9887Proc(report, tag, type, value, &parseInfo->useTDA9887Info, ppErrMsg))
    {
        parseInfo->pCurrentCard->bUseTDA9887 = parseInfo->useTDA9887Info.useTDA9887;
        if (parseInfo->pCurrentCard->bUseTDA9887)
        {
            int i;
#if 0  //always enter for loop to clear unused masks
            int count = 0;
            // Count the number of non-zero masks.
            for (i = 0; i < TDA9887_FORMAT_LASTONE; i++)
            {
                if (parseInfo->useTDA9887Info.tdaModes[i].mask != 0)
                {
                    count++;
                }
            }
            // If there are any non-zero mask.
            //if (count > 0)
#endif
            {
                // Transfer all data to the vector.
                for (i = 0; i < TDA9887_FORMAT_LASTONE; i++)
                {
                    TTDA9887FormatModes * modes = &parseInfo->pCurrentCard->tda9887Modes[i];

                    if (parseInfo->useTDA9887Info.tdaModes[i].mask != 0)
                    {
                        modes->format = (eTDA9887Format)i;
                        modes->mask = parseInfo->useTDA9887Info.tdaModes[i].mask;
                        modes->bits = parseInfo->useTDA9887Info.tdaModes[i].bits;
                    }
                    else
                    {   // mark unused list elements as invalid
                        modes->format = TDA9887_FORMAT_NONE;
                        modes->mask = 0;
                        modes->bits = 0;
                    }
                }
            }
        }
    }

    if (*ppErrMsg != NULL)
        return FALSE;
    else
        return TRUE;
}

static bool ReadCardDefaultTunerProc(int report, const CParseTag* tag, unsigned char type,
                                     const CParseValue* value, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;

    // Return TRUE means parseInfo->tunerInfo is ready.
    if (ReadTunerProc(report, tag, type, value, &parseInfo->tunerInfo, ppErrMsg))
    {
        if (parseInfo->tunerInfo.tunerId == TUNER_AUTODETECT ||
            parseInfo->tunerInfo.tunerId == TUNER_USER_SETUP)
        {
            *ppErrMsg = ("Tuner id \"auto\" and \"setup\" are not supported.");
            return FALSE;
        }
        parseInfo->pCurrentCard->TunerId = parseInfo->tunerInfo.tunerId;
    }
    if (*ppErrMsg != NULL)
        return FALSE;
    else
        return TRUE;
}


static bool ReadCardInfoProc(int report, const CParseTag* tag, unsigned char dummy1,
                             const CParseValue* value, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;
    TCardType* pCardList;

    if (report != REPORT_VALUE)
    {
        return TRUE;
    }

    // Name
    if (tag == k_parseCard + 0)
    {
        if (CParseValue_GetString(value) == '\0')
        {
            *ppErrMsg = ("\"\" is not a valid name of a card");
            return FALSE;
        }
        pCardList = CList_GetFront(parseInfo->pCardList);
        while (pCardList != NULL)
        {
            if (stricmp(pCardList->szName, CParseValue_GetString(value)) == 0)
            {
                *ppErrMsg = ("A card was already specified with this name");
                return FALSE;
            }
            pCardList = CList_GetNext(pCardList);
        }
        strncpy(parseInfo->pCurrentCard->szName, CParseValue_GetString(value),
                sizeof(parseInfo->pCurrentCard->szName) - 1);
    }
    // DeviceID
    else if (tag == k_parseCard + 1)
    {
        int n = CParseValue_GetNumber(value);
        if (n != 0x7130 && n != 0x7134 && n != 0x7133)
        {
            *ppErrMsg = ("DeviceID needs to be either 0x7134 or 0x7130");
            return FALSE;
        }
        parseInfo->pCurrentCard->DeviceId = (WORD)(n);
    }
    // AudioCrystal
    else if (tag == k_parseCard + 3)
    {
        parseInfo->pCurrentCard->AudioCrystal =
            (eAudioCrystal)CParseValue_GetNumber(value);
    }
    // GPIOMask
    else if (tag == k_parseCard + 4)
    {
        parseInfo->pCurrentCard->dwGPIOMode = (DWORD)CParseValue_GetNumber(value);
    }
    return TRUE;
}

static bool ReadCardAutoDetectIDProc(int report, const CParseTag* tag, unsigned char dummy1,
                                     const CParseValue* value, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;
    int i;

    if (report != REPORT_VALUE)
    {
        return TRUE;
    }

    i = (int)(tag - k_parseAutoDetectID);
    parseInfo->pCurrentCard->AutoDetectId[i] = (DWORD)CParseValue_GetNumber(value);

    return TRUE;
}


static bool ReadCardProc(int report, const CParseTag* dummy1, unsigned char dummy2,
                         const CParseValue* dummy3, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;
    long finalCount;
    int i;

    switch (report)
    {
    case REPORT_OPEN:
        parseInfo->pCurrentCard = CList_PushBack(&parseInfo->pCardList, &m_SAA7134CardDefaults);
        break;
    case REPORT_CLOSE:
        {
            if (parseInfo->pCurrentCard->DeviceId == 0x7130)
            {
                if (parseInfo->pCurrentCard->AudioCrystal != AUDIOCRYSTAL_NONE)
                {
                    *ppErrMsg = ("AudioCrystal for a 0x7130 card must be NONE");
                    return FALSE;
                }
            }

            finalCount = 0;
            for (i = 0; i < parseInfo->pCurrentCard->NumInputs; i++)
            {
                if (parseInfo->pCurrentCard->Inputs[i].InputType == INPUTTYPE_FINAL)
                {
                    finalCount++;
                }
            }
            if (finalCount > 1)
            {
                *ppErrMsg = ("There can only be one input of type FINAL");
                return FALSE;
            }
            if (finalCount == 1)
            {
                i = parseInfo->pCurrentCard->NumInputs - 1;
                if (parseInfo->pCurrentCard->Inputs[i].InputType != INPUTTYPE_FINAL)
                {
                    *ppErrMsg = ("The FINAL input must be after all other inputs");
                    return FALSE;
                }
            }

            parseInfo->nGoodCards++;
            parseInfo->pCurrentCard = NULL;
        }
        break;
    }
    return TRUE;
}


#if 0
BOOL APIENTRY ParseErrorProc(HWND hDlg, UINT message, UINT wParam, LPARAM lParam)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)lParam;

    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetWindowTextA(hDlg, "SAA713x Card List Parsing Error");

            HWND hItem;
            hItem = GetDlgItem(hDlg, IDC_ERROR_MESSAGE);
            if (CHCParser::IsUnicodeOS())
            {
                SetWindowTextW(hItem, parseInfo->pHCParser->GetErrorUnicode().c_str());
            }
            else
            {
                SetWindowTextA(hItem, parseInfo->pHCParser->GetError().c_str());
            }

            ostringstream oss;
            oss << "An error occured while reading SAA713x cards from 'SAA713xCards.ini':";

            hItem = GetDlgItem(hDlg, IDC_TOP_STATIC);
            SetWindowTextA(hItem, oss.str().c_str());

            oss.str("");
            oss << (parseInfo->nGoodCards - 1) << " card(s) were successfully read before this "
                "error.  Although this error is not fatal, if a previously selected SAA713x "
                "card is not among those successfully read, and this error is ignored, a "
                "different card will need to be selected.";

            hItem = GetDlgItem(hDlg, IDC_BOTTOM_STATIC);
            SetWindowTextA(hItem, oss.str().c_str());
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
        case IDIGNORE:
            EndDialog(hDlg, IDIGNORE);
            break;
        case IDRETRY:
        case IDABORT:
            EndDialog(hDlg, LOWORD(wParam));
        }
        break;
    }
    return FALSE;
}
#endif


//
// Notes:
//
// "Might req mode 6": S-Video is listed with VIDEOINPUTSOURCE_PIN0 but what
// is actually used is not mode 0 but mode 8.  --This is due to an old design
// decision that I no longer remember why.  Mode 6 is exactly the same as mode
// 8 except the C-channel gain control is set with a register instead of
// automatic gain control that is linked to the Y-channel. "Might req mode 6"
// has been placed beside entries where the RegSpy dump showed the
// SAA7134_ANALOG_IN_CTRL1 register with xxxx0110(6) instead of xxxx1000(8).
//

//
// LifeView Clones:  (Actually, I don't know who supplies who)
//
//              0x7130                      0x7134                          0x7133
//
// 0x01384e42   Dazzle My TV                LifeView FlyVideo3000
//                                          Chronos Video Shuttle II
//
//
// 0x01385168   LifeView FlyVideo2000       LifeView PrimeTV 7134     PrimeTV 7133
//              Chronos Video Shuttle II
//
// 0x013819d0   V-Gear MyTV2 Radio
//
//
// Notes:
// - The auto detect ID 0x01384e42 is not used by LifeView FlyVideo3000 that I know
//   of.  This ID comes from the SAA7134 version of Chronos Video Shuttle II.
// - All cards above have an identical video input pin configuration.  They also use
//   the same 0x0018e700 GPIO mask.
// - "Genius Video Wonder PRO III" also has the ID 0x01385168 and is 0x7134 but the
//   audio clock is different.
//

#if 0
static int GetMaxCards( void )
{
    InitializeSAA713xUnknownCard();
    return CList_GetSize(m_SAA713xCards);
}
#endif

#if 0
static int GetCardByName(LPCSTR cardName)
{
    TCardType* pCardList;
    int i = 0;

    pCardList = CList_GetFront(parseInfo->pCardList);
    while (pCardList != NULL)
    {
        if (stricmp(pCardList->szName, cardName) == 0)
        {
            return i;
        }
        pCardList = CList_GetNext(pCardList);
        i++;
    }
    return 0;
}
#endif

static uint AutoDetectCardType( TVCARD * pTvCard )
{
    TCardType* pCardList;
    WORD  DeviceId;
    DWORD SubSystemId;
    uint i;
    uint j;

    INIT_CARD_LIST();
    InitializeSAA713xUnknownCard();

    if (pTvCard != NULL)
    {
        DeviceId           = pTvCard->params.DeviceId;
        SubSystemId        = pTvCard->params.SubSystemId;

        if (SubSystemId == 0x00001131)
        {
            LOG(0, "SAA713x: Autodetect found [0x00001131] *Unknown Card*.");
            return SAA7134CARDID_UNKNOWN;
        }

        pCardList = CList_GetFront(m_SAA713xCards);
        i = 0;
        while (pCardList != NULL)
        {
            for (j = 0; j < SA_AUTODETECT_ID_PER_CARD && pCardList->AutoDetectId[j] != 0; j++)
            {
                if (pCardList->DeviceId == DeviceId &&
                    pCardList->AutoDetectId[j] == SubSystemId)
                {
                    dprintf1("SAA713x: Autodetect found %s.", pCardList->szName);
                    return i;
                }
            }
            i++;
        }

        ifdebug2(SubSystemId != 0, "SAA713x: unknown card 0x%04X %08lX", DeviceId, SubSystemId);

        LOG(0, "SAA713x: Autodetect found an unknown card with the following");
        LOG(0, "SAA713x: properties.  Please email the author and quote the");
        LOG(0, "SAA713x: following numbers, as well as which card you have,");
        LOG(0, "SAA713x: so it can be added to the list:");
        LOG(0, "SAA713x: DeviceId: 0x%04x, AutoDetectId: 0x%08x",
            DeviceId, SubSystemId);
    }
    else
        fatal0("SAA7134-AutoDetectCardType: illegal NULL ptr param");

    return SAA7134CARDID_UNKNOWN;
}

static uint AutoDetectTuner( TVCARD * pTvCard, uint CardId )
{
    TCardType* pCardList;
    uint TunerId = 0;

    INIT_CARD_LIST();

    pCardList = CList_Get(m_SAA713xCards, CardId);
    if (pCardList != NULL)
    {
        TunerId = pCardList->TunerId;
    }
    else
        debug1("AutoDetectTuner: invalid card index %d", CardId);

    return TunerId;
}


static bool GetTda9887Modes( TVCARD * pTvCard, bool * pHasTda9887, void ** ppModes )
{
    TCardType* pCardList;
    uint  m_CardType;

    INIT_CARD_LIST();

    if ((pTvCard != NULL) && (pHasTda9887 != NULL) && (ppModes != NULL))
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if (pCardList != NULL)
        {
            *pHasTda9887 = pCardList->bUseTDA9887;

            if (pCardList->bUseTDA9887)
                *ppModes = &pCardList->tda9887Modes[0];
            else
                *ppModes = NULL;
        }
        else
            debug2("SA7134-GetTda9887Modes: invalid card idx %d (>= %d)", m_CardType, CList_GetSize(m_SAA713xCards));
    }
    else
        fatal0("SAA7134-GetTda9887Modes: illegal NULL ptr param");

    return TRUE;
}

static void GetIffType( TVCARD * pTvCard, bool * pIsPinnacle, bool * pIsMono )
{
    INIT_CARD_LIST();

    if ((pTvCard != NULL) && (pIsPinnacle != NULL) && (pIsMono != NULL))
    {
        // obsolete - replaced by GetTda9887Modes
    }
    else
        fatal0("SAA7134-GetIffType: illegal NULL ptr param");
}

static uint GetPllType( TVCARD * pTvCard, uint CardId )
{
    INIT_CARD_LIST();

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
    TCardType* pCardList;
    uint  m_CardType;
    uint  count = 0;

    INIT_CARD_LIST();

    // There must be at most one input of type INPUTTYPE_FINAL in a card
    // and that input MUST always be the last.  GetNumInputs() is the
    // only function that compensates for the FINAL input to subtract
    // from the number of actual inputs.

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if (pCardList != NULL)
        {
            count = pCardList->NumInputs;

            if ((count > 0) && (pCardList->Inputs[count - 1].InputType == INPUTTYPE_FINAL))
            {
                count -= 1;
            }
        }
        else
            debug2("SA7134-GetNumInputs: invalid card idx %d (>= %d)", m_CardType, CList_GetSize(m_SAA713xCards));
    }
    else
        fatal0("SAA7134-GetNumInputs: illegal NULL ptr param");

    return count;
}

static const char * GetInputName( TVCARD * pTvCard, uint nInput )
{
    TCardType* pCardList;
    uint  m_CardType;
    const char * pName = NULL;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            pName = pCardList->Inputs[nInput].szName;
        }
        else
            debug4("SAA7134-GetInputName: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_SAA713xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("SAA7134-GetInputName: illegal NULL ptr param");

    return pName;
}


static bool IsInputATuner( TVCARD * pTvCard, uint nInput )
{
    TCardType* pCardList;
    uint  m_CardType;
    bool  result = FALSE;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            result = (pCardList->Inputs[nInput].InputType == INPUTTYPE_TUNER);
        }
        else
            debug4("SA7134-IsInputATuner: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_SAA713xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("SAA7134-IsInputATuner: illegal NULL ptr param");

    return result;
}


static bool IsInputSVideo( TVCARD * pTvCard, uint nInput )
{
    TCardType* pCardList;
    uint  m_CardType;
    bool  result = FALSE;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            result = (pCardList->Inputs[nInput].InputType == INPUTTYPE_SVIDEO);
        }
        else
            debug4("SA7134-IsInputSVideo: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_SAA713xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("SAA7134-IsInputSVideo: illegal NULL ptr param");

    return result;
}


static const char * GetCardName( TVCARD * pTvCard, uint m_CardType )
{
    TCardType* pCardList;
    const char * pName = NULL;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if (pCardList != NULL)
        {
            pName = pCardList->szName;
        }
        //else: no warning because during card enumeration this function
        // is called until NULL is returned
    }
    else
        fatal0("SAA7134-GetCardName: illegal NULL ptr param");

    return pName;
}


static bool SetVideoSource( TVCARD * pTvCard, uint nInput )
{
    TCardType* pCardList;
    uint  m_CardType;
    bool  result = FALSE;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_SAA713xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            StandardSAA7134InputSelect(pTvCard, nInput);
            result = TRUE;
        }
        else
            debug4("SA7134-SetVideoSource: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_SAA713xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("SAA7134-SetVideoSource: illegal NULL ptr param");

    return result;
}


static void StandardSAA7134InputSelect(TVCARD * pTvCard, int nInput)
{
    TCardType* pCardList;
    eVideoInputSource VideoInput;
    uint  m_CardType = pTvCard->params.cardId;
    BYTE Mode;

    pCardList = CList_Get(m_SAA713xCards, m_CardType);
    if (pCardList != NULL)
    {
        // -1 for finishing clean up
        if(nInput == -1)
        {
            if ((pCardList->NumInputs > 0) &&
                (pCardList->Inputs[pCardList->NumInputs - 1].InputType == INPUTTYPE_FINAL))
            {
                nInput = pCardList->NumInputs - 1;
            }
            else
            {   // There are no cleanup specific changes
                return;
            }
        }

        if(nInput >= pCardList->NumInputs)
        {
            debug1("Input Select Called for invalid input %d", nInput);
            nInput = pCardList->NumInputs - 1;
        }
        if(nInput < 0)
        {
            debug1("Input Select Called for invalid input %d", nInput);
            nInput = 0;
        }

        VideoInput = pCardList->Inputs[nInput].VideoInputPin;

        /// There is a 1:1 correlation between (int)eVideoInputSource
        /// and SAA7134_ANALOG_IN_CTRL1_MODE
        Mode = (VideoInput == VIDEOINPUTSOURCE_NONE) ? 0x00 : VideoInput;

        switch (pCardList->Inputs[nInput].InputType)
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
        if (pCardList->dwGPIOMode != 0)
        {
            MaskDataDword(SAA7134_GPIO_GPMODE, pCardList->dwGPIOMode, 0x0EFFFFFF);
            MaskDataDword(SAA7134_GPIO_GPSTATUS,
                          pCardList->Inputs[nInput].dwGPIOStatusBits,
                          pCardList->Inputs[nInput].dwGPIOStatusMask);
        }
    }
    else
        debug2("StandardSAA7134-InputSelect: invalid card idx %d (>= %d)", m_CardType, CList_GetSize(m_SAA713xCards));
}

// ---------------------------------------------------------------------------
// Free allocated resources
//
static void FreeCardList( void )
{
   if (m_SAA713xCards != NULL)
   {
       CList_Destroy(&m_SAA713xCards);
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
   GetTda9887Modes,
   GetIffType,
   GetPllType,
   SupportsAcpi,
   GetNumInputs,
   GetInputName,
   IsInputATuner,
   IsInputSVideo,
   SetVideoSource,
   FreeCardList,
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

