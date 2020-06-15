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
 *  DScaler #Id: CX2388xCard_Types.cpp,v 1.31 2004/12/25 22:40:18 to_see Exp #
 *  DScaler #Id: CX2388xCard_Tuner.cpp,v 1.9 2005/12/27 19:29:35 to_see Exp #
 *
 *  $Id: cx2388x_typ.c,v 1.22 2020/06/17 08:19:07 tom Exp tom $
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
#include "dsdrv/cx2388x_reg.h"
#include "dsdrv/cx2388x_typ.h"


typedef enum              // Different types of input currently supported
{
    INPUTTYPE_TUNER,      // standard composite input
    INPUTTYPE_COMPOSITE,  // standard composite input
    INPUTTYPE_SVIDEO,     // standard s-video input
    INPUTTYPE_CCIR,       // Digital CCIR656 input on the GPIO pins
    INPUTTYPE_COLOURBARS, // Shows Colour Bars for testing
    INPUTTYPE_FINAL,      // Stores the state the cards should be put into at the end
} eInputType;

typedef struct
{
    DWORD GPIO_0;
    DWORD GPIO_1;
    DWORD GPIO_2;
    DWORD GPIO_3;
} TGPIOSet;

typedef struct             // Defines each input on a card
{
    char       szName[64]; // Name of the input
    eInputType InputType;  // Type of the input
    BYTE       MuxSelect;  // Which mux on the card is to be used
    TGPIOSet   GPIOSet;    // Which GPIO's on the card is to be used
} TInputType;

typedef enum
{
    MODE_STANDARD = 0,
    MODE_H3D,
} eCardMode;

#define CX_INPUTS_PER_CARD        9
#define CX_AUTODETECT_ID_PER_CARD 3

typedef struct TCardType_struct   // Defines the specific settings for a given card
{
    struct TCardType_struct * pNext;

    char        szName[128];
    eCardMode   CardMode;
    uint        NumInputs;
    TInputType  Inputs[CX_INPUTS_PER_CARD];
    eTunerId    TunerId;
    DWORD       AutoDetectId[CX_AUTODETECT_ID_PER_CARD];
    BOOL        bUseTDA9887;
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

static bool ReadCardInputInfoProc(int report, const CParseTag* tag, unsigned char dummy1,
                                  const CParseValue* value, void* context, char **ppErrMsg);
static bool ReadCardInputProc(int report, const CParseTag* tag, unsigned char dummy1,
                              const CParseValue* dummy2, void* context, char **ppErrMsg);
static bool ReadCardUseTDA9887Proc(int report, const CParseTag* tag, unsigned char type,
                                   const CParseValue* value, void* context, char **ppErrMsg);
static bool ReadCardDefaultTunerProc(int report, const CParseTag* tag, unsigned char type,
                                     const CParseValue* value, void* context, char **ppErrMsg);
static bool ReadCardInfoProc(int report, const CParseTag* tag, unsigned char dummy1,
                             const CParseValue* value, void* context, char **ppErrMsg);
static bool ReadCardAutoDetectIDProc(int report, const CParseTag* tag, unsigned char dummy1,
                                     const CParseValue* value, void* context, char **ppErrMsg);
static bool ReadCardProc(int report, const CParseTag* dummmy1, unsigned char dummmy2,
                         const CParseValue* dummmy3, void* context, char **ppErrMsg);
static BOOL InitializeCX2388xCardList( void );
static void InitializeCX2388xUnknownCard( void );
static void StandardInputSelect( TVCARD * pTvCard, uint nInput);

static const char* const k_CX2388xCardListFilename = "CX2388xCards.ini";

#define CX2388xCARD_UNKNOWN 0
static const TCardType m_CX2388xUnknownCard = 
{
    NULL,
    "*Unknown Card*",
    MODE_STANDARD,
    4,
    {
        {
            "Tuner",
            INPUTTYPE_TUNER,
            0,
            { 0 },
        },
        {
            "Composite",
            INPUTTYPE_COMPOSITE,
            1,
            { 0 },
        },
        {
            "S-Video",
            INPUTTYPE_SVIDEO,
            2,
            { 0 },
        },
        {
            "Colour Bars",
            INPUTTYPE_COLOURBARS,
            0,
            { 0 },
        },
    },
    TUNER_PHILIPS_NTSC,
    { 0 },
    FALSE,
    { {TDA9887_FORMAT_NONE, 0, 0}, }
};

static const TCardType m_CX2388xCardDefaults =
{
    NULL, "", MODE_STANDARD, 0, { }, TUNER_ABSENT, { }, FALSE, { }
};

static TCardType * m_CX2388xCards = NULL;

#define INIT_CARD_LIST() do{if (m_CX2388xCards == NULL) {InitializeCX2388xCardList();}}while(0)

//////////////////////////////////////////////////////////////////////////
// CX2388x card list parsing constants
//////////////////////////////////////////////////////////////////////////
const CParseConstant k_parseInputTypeConstants[] =
{
    PCINT( "TUNER",      INPUTTYPE_TUNER      ),
    PCINT( "COMPOSITE",  INPUTTYPE_COMPOSITE  ),
    PCINT( "SVIDEO",     INPUTTYPE_SVIDEO     ),
    PCINT( "CCIR",       INPUTTYPE_CCIR       ),
    PCINT( "COLOURBARS", INPUTTYPE_COLOURBARS ),
    PCEND
};

const CParseConstant k_parseCardModeConstants[] =
{
    PCINT( "STANDARD",   MODE_STANDARD ),
    PCINT( "H3D",        MODE_H3D      ),
    PCEND
};

//////////////////////////////////////////////////////////////////////////
// CX2388x card list parsing values
//////////////////////////////////////////////////////////////////////////
const CParseTag k_parseCardGPIOSet[] =
{
    PTLEAF(  "GPIO_0", PARSE_NUMERIC, 1, 16, NULL, ReadCardInputInfoProc ),
    PTLEAF(  "GPIO_1", PARSE_NUMERIC, 0, 16, NULL, ReadCardInputInfoProc ),
    PTLEAF(  "GPIO_2", PARSE_NUMERIC, 0, 16, NULL, ReadCardInputInfoProc ),
    PTLEAF(  "GPIO_3", PARSE_NUMERIC, 0, 16, NULL, ReadCardInputInfoProc ),
    PTEND
};

const CParseTag k_parseCardInput[] =
{
    PTLEAF(  "Name",       PARSE_STRING,   1, 63, NULL,                      ReadCardInputInfoProc ),
    PTCONST( "Type",       PARSE_CONSTANT, 1, 16, k_parseInputTypeConstants, ReadCardInputInfoProc ),
    PTLEAF(  "MuxSelect",  PARSE_NUMERIC,  1,  1, NULL,                      ReadCardInputInfoProc ),
    PTCHILD( "GPIOSet",    PARSE_CHILDREN, 0,  1, k_parseCardGPIOSet,        NULL ),
    PTEND
};

const CParseTag k_parseCardAutoDetectID[] =
{
    PTLEAF(  "0", PARSE_NUMERIC, 0, 16, NULL, ReadCardAutoDetectIDProc ),
    PTLEAF(  "1", PARSE_NUMERIC, 0, 16, NULL, ReadCardAutoDetectIDProc ),
    PTLEAF(  "2", PARSE_NUMERIC, 0, 16, NULL, ReadCardAutoDetectIDProc ),
    PTEND
};

const CParseTag k_parseCard[] =
{
    PTLEAF(  "Name",           PARSE_STRING,                 1, 127,                NULL,                     ReadCardInfoProc         ), 
    PTCONST( "CardMode",       PARSE_CONSTANT,               0, 32,                 k_parseCardModeConstants, ReadCardInfoProc         ),
    PTCONST( "DefaultTuner",   PARSE_CONSTANT|PARSE_NUMERIC, 0, 32,                 k_parseTunerConstants,    ReadCardDefaultTunerProc ),
    PTCHILD( "AutoDetectID",   PARSE_CHILDREN,               0, 1,                  k_parseCardAutoDetectID,  NULL                     ),
    PTCHILD( "Input",          PARSE_CHILDREN,               0, CX_INPUTS_PER_CARD, k_parseCardInput,         ReadCardInputProc        ),
    PTCHILD( "Final",          PARSE_CHILDREN,               0, CX_INPUTS_PER_CARD, k_parseCardInput+2,       ReadCardInputProc        ),
    PTCHILD( "UseTDA9887",     PARSE_CHILDREN,               0, 1,                  k_parseUseTDA9887,        ReadCardUseTDA9887Proc   ),
    PTEND
};

const CParseTag k_parseCardList[] =
{
    PTCHILD( "Card", PARSE_CHILDREN, 0, 1024, k_parseCard, ReadCardProc ),
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
        const char * name = CParseValue_GetString(value);
        if (name == NULL)
        {
            *ppErrMsg = ("input name is not a string");
            return FALSE;
        }
        else if (*name == '\0')
        {
            *ppErrMsg = ("\"\" is not a valid name of an input");
            return FALSE;
        }
        strncpy(input->szName, name, sizeof(input->szName) - 1);
    }

    // Input Type
    else if (tag == k_parseCardInput + 1)
    {
        input->InputType = (eInputType)CParseValue_GetNumber(value);
    }

    // Mux Select
    else if (tag == k_parseCardInput + 2)
    {
        // 0...3
        int n = CParseValue_GetNumber(value);
        if (n < 0 || n > 3)
        {
            *ppErrMsg = ("MuxSelect must be between 0 and 3");
            return FALSE;
        }

        input->MuxSelect = n;
    }

    // GPIOSet->GPIO_0
    else if (tag == k_parseCardGPIOSet + 0)
    {
        input->GPIOSet.GPIO_0 = (DWORD)CParseValue_GetNumber(value);
    }

    // GPIOSet->GPIO_1
    else if (tag == k_parseCardGPIOSet + 1)
    {
        input->GPIOSet.GPIO_1 = (DWORD)CParseValue_GetNumber(value);
    }
    
    // GPIOSet->GPIO_2
    else if (tag == k_parseCardGPIOSet + 2)
    {
        input->GPIOSet.GPIO_2 = (DWORD)CParseValue_GetNumber(value);
    }

    // GPIOSet->GPIO_3
    else if (tag == k_parseCardGPIOSet + 3)
    {
        input->GPIOSet.GPIO_3 = (DWORD)CParseValue_GetNumber(value);
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
            input->InputType = (tag == k_parseCard + 5) ? INPUTTYPE_FINAL : INPUTTYPE_COMPOSITE;
            input->GPIOSet.GPIO_0 = 0;
            input->GPIOSet.GPIO_1 = 0;
            input->GPIOSet.GPIO_2 = 0;
            input->GPIOSet.GPIO_3 = 0;
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
            for (int i = 0; i < TDA9887_FORMAT_LASTONE; i++)
            {
                if (parseInfo->useTDA9887Info.tdaModes[i].mask != 0)
                {
                    count++;
                }
            }
            // If there are any non-zero mask.
            if (count > 0)
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
        const char * name = CParseValue_GetString(value);
        if (name == NULL)
        {
            *ppErrMsg = ("card name is not a string");
            return FALSE;
        }
        else if (*name == '\0')
        {
            *ppErrMsg = ("\"\" is not a valid name of a card");
            return FALSE;
        }
        
        pCardList = CList_GetFront(parseInfo->pCardList);
        while (pCardList != NULL)
        {
            if (stricmp(pCardList->szName, name) == 0)
            {
                *ppErrMsg = ("A card was already specified with this name");
                return FALSE;
            }
            pCardList = CList_GetNext(pCardList);
        }
        
        strncpy(parseInfo->pCurrentCard->szName, name,
                sizeof(parseInfo->pCurrentCard->szName) - 1);
    }

    // Card Mode
    else if (tag == k_parseCard + 1)
    {
        parseInfo->pCurrentCard->CardMode = (eCardMode)(CParseValue_GetNumber(value));
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

    i = (int)(tag - k_parseCardAutoDetectID);
    parseInfo->pCurrentCard->AutoDetectId[i] = (DWORD)(CParseValue_GetNumber(value));

    return TRUE;
}

static bool ReadCardProc(int report, const CParseTag* dummmy1, unsigned char dummmy2,
                         const CParseValue* dummmy3, void* context, char **ppErrMsg)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)context;

    switch (report)
    {
    case REPORT_OPEN:
        parseInfo->pCurrentCard = CList_PushBack(&parseInfo->pCardList, &m_CX2388xCardDefaults);
        break;
    case REPORT_CLOSE:
        {
            long finalCount = 0;
            int i;
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
                int i = parseInfo->pCurrentCard->NumInputs - 1;
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
BOOL APIENTRY CCX2388xCard::ParseErrorProc(HWND hDlg, UINT message, UINT wParam, LPARAM lParam)
{
    TParseCardInfo* parseInfo = (TParseCardInfo*)lParam;

    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetWindowTextA(hDlg, "CX2388x Card List Parsing Error");

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
            oss << "An error occured while reading CX2388x cards from 'CX2388xCards.ini':";

            hItem = GetDlgItem(hDlg, IDC_TOP_STATIC);
            SetWindowTextA(hItem, oss.str().c_str());

            oss.str("");
            oss << (parseInfo->nGoodCards - 1) << " card(s) were successfully read before this "
                "error.  Although this error is not fatal, if a previously selected CX2388x "
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


static BOOL InitializeCX2388xCardList( void )
{
    TParseCardInfo parseInfo;
    BOOL result = TRUE;

    InitializeCX2388xUnknownCard();

    HCParser_Init(k_parseCardList);

    parseInfo.pCardList = m_CX2388xCards;
    parseInfo.pCurrentCard = NULL;
    parseInfo.nGoodCards = 1;

    // No need to use ParseLocalFile() because DScaler does SetExeDirectory()
    // at the beginning.
    while (!HCParser_ParseFile(k_CX2388xCardListFilename, (void*)&parseInfo, FALSE))
    {
        DWORD iRetVal;
        char * pMsg = HCParser_GetError();

        if (pMsg != NULL)
           iRetVal = MessageBox(NULL, pMsg, k_CX2388xCardListFilename, MB_ICONSTOP | MB_ABORTRETRYIGNORE | MB_TASKMODAL | MB_SETFOREGROUND);
        else
           iRetVal = MessageBox(NULL, "Failed to read card definition file.", k_CX2388xCardListFilename, MB_ICONSTOP | MB_ABORTRETRYIGNORE | MB_TASKMODAL | MB_SETFOREGROUND);

        if (iRetVal == IDABORT)
        {
            result = FALSE;
            break;
        }

        // retry parsing; discard all entries except for the "unknown" type
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

       LOG(1, "CX2388x cardlist: %lu card(s) read", parseInfo.nGoodCards);
    }

    HCParser_Destroy();

    return result;
}

static void InitializeCX2388xUnknownCard( void )
{
    if (m_CX2388xCards == NULL)
    {
        CList_PushBack(&m_CX2388xCards, &m_CX2388xUnknownCard);
    }
}

static const char * GetCardName( TVCARD * pTvCard, uint CardId )
{
    TCardType* pCardList;
    const char * pName = NULL;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        pCardList = CList_Get(m_CX2388xCards, CardId);
        if (pCardList != NULL)
        {
            pName = pCardList->szName;
        }
        //else: no warning because during card enumeration this function
        // is called until NULL is returned
    }
    else
        fatal0("Cx2388x-GetCardName: illegal NULL ptr param");

    return pName;
}

static bool GetTda9887Modes( TVCARD * pTvCard, bool * pHasTda9887, const TTDA9887FormatModes ** ppModes )
{
    TCardType* pCardList;
    uint  m_CardType;

    INIT_CARD_LIST();

    if ((pTvCard != NULL) && (pHasTda9887 != NULL) && (ppModes != NULL))
    {
        m_CardType = pTvCard->params.cardId;

        pCardList = CList_Get(m_CX2388xCards, m_CardType);
        if (pCardList != NULL)
        {
            *pHasTda9887 = pCardList->bUseTDA9887;

            if (pCardList->bUseTDA9887)
                *ppModes = &pCardList->tda9887Modes[0];
            else
                *ppModes = NULL;
        }
        else
            debug2("Cx2388x-GetTda9887Modes: invalid card idx %d (>= %d)", m_CardType, CList_GetSize(m_CX2388xCards));
    }
    else
        fatal0("Cx2388x-GetTda9887Modes: illegal NULL ptr param");

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
        fatal0("Cx2388x-GetIffType: illegal NULL ptr param");
}

static uint GetPllType( TVCARD * pTvCard, uint CardId )
{
    INIT_CARD_LIST();

    if (pTvCard == NULL)
        fatal0("Cx2388x-GetPllType: illegal NULL ptr param");

    return 0;
}

// ---------------------------------------------------------------------------
// Query if the PCI card supports ACPI (power management)
//
static bool SupportsAcpi( TVCARD * pTvCard )
{
    INIT_CARD_LIST();

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

        pCardList = CList_Get(m_CX2388xCards, m_CardType);
        if (pCardList != NULL)
        {
            count = pCardList->NumInputs;

            if ((count > 0) && (pCardList->Inputs[count - 1].InputType == INPUTTYPE_FINAL))
            {
                count -= 1;
            }
        }
        else
            debug2("Cx2388x-GetNumInputs: invalid card idx %d (>= %d)", m_CardType, CList_GetSize(m_CX2388xCards));
    }
    else
        fatal0("Cx2388x-GetNumInputs: illegal NULL ptr param");

    return count;
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

        pCardList = CList_Get(m_CX2388xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            result = (pCardList->Inputs[nInput].InputType == INPUTTYPE_TUNER);
        }
        else
            debug4("Cx2388x-IsInputATuner: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_CX2388xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("Cx2388x-IsInputATuner: illegal NULL ptr param");

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

        pCardList = CList_Get(m_CX2388xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            result = (pCardList->Inputs[nInput].InputType == INPUTTYPE_SVIDEO);
        }
        else
            debug4("Cx2388x-IsInputSVideo: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_CX2388xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("Cx2388x-IsInputSVideo: illegal NULL ptr param");

    return result;
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

        pCardList = CList_Get(m_CX2388xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
           pName = pCardList->Inputs[nInput].szName;
        }
        else
            debug4("Cx2388x-GetInputName: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_CX2388xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("Cx2388x-GetInputName: illegal NULL ptr param");

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

        pCardList = CList_Get(m_CX2388xCards, m_CardType);
        if ((pCardList != NULL) && (nInput < pCardList->NumInputs))
        {
            StandardInputSelect(pTvCard, nInput);
            result = TRUE;
        }
        else
            debug4("Cx2388x-SetVideoSource: invalid card idx %d (>= %d) or input idx %d (>%d)", m_CardType, CList_GetSize(m_CX2388xCards), nInput, pCardList->NumInputs);
    }
    else
        fatal0("Cx2388x-SetVideoSource: illegal NULL ptr param");

    return result;
}

static uint AutoDetectCardType( TVCARD * pTvCard )
{
    TCardType* pCardList;
    DWORD SubSystemId;
    uint CardId;
    uint j;

    INIT_CARD_LIST();

    if (pTvCard != NULL)
    {
        SubSystemId = pTvCard->params.SubSystemId;

        if (SubSystemId != 0 && SubSystemId != 0xffffffff)
        {
            pCardList = CList_GetFront(m_CX2388xCards);
            CardId = 0;
            while (pCardList != NULL)
            {
                for(j = 0; j < CX_AUTODETECT_ID_PER_CARD && pCardList->AutoDetectId[j] != 0; j++)
                {
                    if(pCardList->AutoDetectId[j] == SubSystemId)
                    {
                        LOG(0, "CX2388x: Autodetect found %s.", pCardList->szName);
                        return CardId;
                    }
                }
                pCardList = CList_GetNext(pCardList);
                CardId += 1;
            }
        }
    }
    else
        fatal0("Cx2388x-AutoDetectCardType: illegal NULL ptr param");

    return CX2388xCARD_UNKNOWN;
}

// note: DScaler moved this function to CX2388xCard_Tuner.cpp
static uint AutoDetectTuner( TVCARD * pTvCard, uint CardId )
{
    TCardType* pCardList;

    INIT_CARD_LIST();

    if (pTvCard == NULL)
    {
        fatal0("AutoDetectTuner: illegal NULL ptr param");
        return TUNER_ABSENT;
    }

    pCardList = CList_Get(m_CX2388xCards, CardId);
    if (pCardList == NULL)
    {
        debug1("AutoDetectTuner: invalid card index %d", CardId);
        return TUNER_ABSENT;
    }

    if(pCardList->TunerId == TUNER_USER_SETUP)
    {
        dprintf0("AutoDetectTuner: auto-detection not supported for this card\n");
        return TUNER_ABSENT;
    }
    else if(pCardList->TunerId == TUNER_AUTODETECT)
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

        // Note: string in card-ini is "Hauppauge WinTV 34xxx models"
        #define pszCardHauppaugeAnalog "Hauppauge WinTV 34"

        // Bytes 0:7 are used for PCI SubId, data starts at byte 8
        #define CX_EEPROM_OFFSET 8

        if(strncmp(pCardList->szName, pszCardHauppaugeAnalog, strlen(pszCardHauppaugeAnalog)) == 0)
        {
            if (Eeprom[CX_EEPROM_OFFSET + 0] != 0x84 || Eeprom[CX_EEPROM_OFFSET + 2] != 0)
            {
                //Hauppage EEPROM invalid
                debug2("AutoDetectTuner: Hauppage card. EEPROM error: 0x%02X,0x%02X (!= 0x84,0x00)", Eeprom[CX_EEPROM_OFFSET + 0], Eeprom[CX_EEPROM_OFFSET + 2]);
            }
            else
            {
                Tuner = Tuner_GetHauppaugeEepromId(Eeprom[CX_EEPROM_OFFSET + 9]);
            }
        }
        else
           debug1("AutoDetectTuner: warning: card %d unsupported by tuner auto-detect", CardId);
        
        return Tuner;
    }
    else
    {
        dprintf1("AutoDetectTuner: not necessary, type fixed to %d\n", pCardList->TunerId);
        return pCardList->TunerId;
    }
}

static void StandardInputSelect( TVCARD * pTvCard, uint nInput)
{
    TCardType* pCardList;
    TGPIOSet* pGPIOSet;
    TInputType* pInput;
    uint  m_CardType = pTvCard->params.cardId;

    pCardList = CList_Get(m_CX2388xCards, m_CardType);
    if (pCardList != NULL)
    {
        // -1 for finishing clean up
        if((int)nInput == -1)
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
        if (nInput >= pCardList->NumInputs)
        {
            debug1("Input Select Called for invalid input %d", nInput);
            nInput = pCardList->NumInputs - 1;
        }

        pInput = &pCardList->Inputs[nInput];

        if(pInput->InputType == INPUTTYPE_COLOURBARS)
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
            VideoInput |= (pInput->MuxSelect << CX2388X_VIDEO_INPUT_MUX_SHIFT);

            // set the comp bit for svideo
            switch (pInput->InputType)
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

        pGPIOSet = &pInput->GPIOSet;
        WriteDword(MO_GP3_IO, pGPIOSet->GPIO_3);
        WriteDword(MO_GP2_IO, pGPIOSet->GPIO_2);
        WriteDword(MO_GP1_IO, pGPIOSet->GPIO_1);
        WriteDword(MO_GP0_IO, pGPIOSet->GPIO_0);
    }
    else
        debug2("Cx2388xTyp-StandardInputSelect: invalid card idx %d (>= %d)", m_CardType, CList_GetSize(m_CX2388xCards));
}


// ---------------------------------------------------------------------------
// Free allocated resources
//
static void FreeCardList( void )
{
    if (m_CX2388xCards != NULL)
    {
       CList_Destroy(&m_CX2388xCards);
    }
}

// ---------------------------------------------------------------------------
// Fill interface struct
//
static const TVCARD_CFG Cx2388xTyp_Interface =
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

void Cx2388xTyp_GetInterface( TVCARD * pTvCard )
{
   if (pTvCard != NULL)
   {
      pTvCard->cfg = &Cx2388xTyp_Interface;
   }
   else
      fatal0("Cx2388xTyp-GetInterface: NULL ptr param");
}

