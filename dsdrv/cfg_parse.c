/////////////////////////////////////////////////////////////////////////////
// #Id: HierarchicalConfigParser.cpp,v 1.16 2006/12/13 01:10:01 robmuller Exp #
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
// nxtvepg $Id: cfg_parse.c,v 1.3 2006/12/21 20:10:59 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////
//
// Grammar:
//
//  file := [\[value\]\n] tag-list
//
//  tag-list := value-tag|parent-tag[ tag-list]
//
//  value-tag := tag [= value\n|[=] (value)]
//  parent-tag := tag [= sub-tag-value-list\n|[=] ( sub-tag-value-list ) \ 
//    [= tag-list\n|tag-list|{ tag-list }]]
//
//  sub-tag-value-list := value[, sub-tag-value-list]
//
//  tag := string
//  value := "string"|number|constant
//
//
// Examples:
//
//  tag = value
//  tag [=] ( value )
//
//  tag
//    sub-tag1 ( value )
//  tag ( sub-tag1-value, sub-tag2-value )
//    ...
//  tag ( sub-tag1-value ) = sub-tag2 ( value )
//    ...
//  tag ( ) = sub-tag ( value )
//    ...
//  tag = sub-tag1-value, sub-tag2-value
//

//
// Example of use:
//
// const ParseConstant constants[] =
// {
//   { "Constant1", "Value1" },
//   { "Constant2", "Value2" },
//   { NULL } // Terminating NULL entry.
// };
//
// const ParseTag tagList[] =
// {
//   { "Foo",           PARSE_STRING|PARSE_CONSTANT,    1, 127, NULL, constants, ReadFooProc },
//   { "Bar",           PARSE_CHILDREN,                 1, 127, tagList2, NULL, ReadBarProc },
//   { NULL } // Terminating NULL entry.
// };
//
// CHCParser parser(tagList);
//
// if (!parser.ParseFile("file.ini", NULL))
// {
//   cout << parser.GetError();
// }
//
//
// In this example, ReadFooProc() is called with REPORT_OPEN, REPORT_VALUE and REPORT_CLOSE
// when a "Foo" is parsed.  REPORT_VALUE may not be called if there is no value specified.
// In this case, the default value should be used (or an error thrown).  ReadBarProc() is
// is called when a "Bar" is parsed.  For a PARSE_CHILDREN entry, a REPORT_OPEN will be
// called, and a REPORT_CLOSE only after all child entries have been parsed.  There is no
// REPORT_VALUE for a PARSE_CHILDREN.
//////////////////////////////////////////////////////////////////////////////

#define DEBUG_SWITCH DEBUG_SWITCH_DSDRV
#define DPRINTF_OFF

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "dsdrv/cfg_parse.h"

enum
{
    MAX_LINE_LENGTH     = 512,
    MAX_READ_BUFFER     = 4096,
};

enum
{
    EXPECT_SECTION      = 1 << 0,
    EXPECT_TAG          = 1 << 1,
    EXPECT_VALUE        = 1 << 2,
    EXPECT_EQUAL        = 1 << 3,
    EXPECT_NEXT_COMMA   = 1 << 4,
    EXPECT_OPEN_V       = 1 << 5,
    EXPECT_CLOSE_V      = 1 << 6,
    EXPECT_OPEN_L       = 1 << 7,
    EXPECT_CLOSE_L      = 1 << 8,
    EXPECT_CLOSE_EOL    = 1 << 9,
    EXPECT_CLOSE_COMMA  = 1 << 10,
    EXPECT_MAX          = 11,
};

typedef struct
{
    void                      * pVec;
    uint                        elemSize;
    uint                        elemCount;
} CVector;

typedef struct ParseState_struct
{
    struct ParseState_struct  * pNext;

    const CParseTag*            parseTags;
    long                        paramIndex;
    unsigned short              expect;
    bool                        bracketOpened;
    bool                        iterateValues;
    bool                        passEOL;
    bool                        valueOpened;
    CVector                     openedTagParamCounts;
    CVector                   * paramCounts;
} ParseState;

typedef struct
{
    char                      * pMsg;
    PARSE_ERR_TYPE              type;
} ParseError;

//typedef struct
//{
    char                        m_readBuffer[MAX_READ_BUFFER];
    size_t                      m_bufferPosition;
    size_t                      m_bufferLength;
    char                        m_newlineChar;

    unsigned long               m_lineNumber;
    char                        m_lineBuffer[MAX_LINE_LENGTH];
    char*                       m_linePoint;

    ParseError                  m_parseError;
    bool                        m_parseErrorSet;
    int                         m_debugOutLevel;
    void*                       m_reportContext;

    const CParseTag*            m_tagListEntry;
    CParseTag                   m_rootParent[2];
    ParseState                * m_parseStates;
//} CFG_PARSE_MODULE;
//
//static CFG_PARSE_MODULE m_parser;

// ------------------------------------------------------------------------------
// forward declarations
//
static void InitializeParseState( void );

static long ReadLineIntoBuffer(FILE* fp);

static bool ProcessStream(FILE* fp);
static bool ProcessLine( void );
static bool ProcessSection( void );
static bool ProcessTag( void );
static bool ProcessOpenValueBracket( void );
static bool ProcessCloseValueBracket( void );
static bool ProcessOpenTagListBracket( void );
static bool ProcessCloseTagListBracket( void );
static bool ProcessComma( void );
static bool ProcessEqual( void );
static bool ProcessValue( void );

static bool AcceptValue(const CParseTag* parseTag, unsigned char types,
                        char* value, unsigned long length);

static bool OpenTag(long tagIndex);
static bool CloseTag(bool openNext);
static bool OpenValue(bool withBracket);
static bool OpenTagList(bool withBracket);
static bool CloseValue( void );

static void ParseErrorAppend(PARSE_ERR_TYPE type, const char * pMsg);
static void ParseErrorClear( void );
static bool ParseErrorEmpty( void );
//static void SetParseError(ParseError * error);
//static void AddExpectLine(ParseError * error, bool debugging);

static void CParseValue_AssignString( CParseValue * parse_value, const char * value );
//static void CParseValue_AssignInt( CParseValue * parse_value, int value );

static bool ReportTag(const CParseTag* parseTag);
static bool ReportOpen(const CParseTag* parseTag);
static bool ReportClose(const CParseTag* parseTag);
static bool ReportValue(const CParseTag* parseTag, unsigned char type,
                        const CParseValue* value, int report);

static void TrimLeft( void );
static bool TagTakesValues(const CParseTag* parseTag);
static long GetNextIterateValuesIndex( void );

static bool IsSpace(int c);
static bool IsAlpha(int c);
static bool IsAlnum(int c);
static bool IsDigit(int c);
static bool IsDelim(int c);
static bool IsHex(int c);

// ------------------------------------------------------------------------------
// List implementation (parser stack)
//
typedef void (CLIST_DESTROY_PROC) ( ParseState * pParseState );

//list<ParseState>::iterator it = m_parseStates.begin();
static void CList_Init( ParseState ** ppList )
{
    *ppList = NULL;
}
static void CList_Destroy( ParseState ** ppList, CLIST_DESTROY_PROC * pCb )
{
    ParseState * pWalk;
    ParseState * pNext;

    pWalk = *ppList;
    while (pWalk != NULL)
    {
        pNext = pWalk->pNext;
        if (pCb != NULL)
        {
            pCb(pWalk);
        }
        xfree(pWalk);
        pWalk = pNext;
    }
    *ppList = NULL;
}

//m_parseStates.front.
static ParseState * CList_GetFront( ParseState * pList )
{
    return pList;
}
static ParseState * CList_GetNext( ParseState * pList )
{
    return pList->pNext;
}
//m_parseStates.size
static uint CList_GetSize( ParseState * pList )
{
    uint size = 0;

    while (pList != NULL)
    {
        size += 1;
        pList = pList->pNext;
    }
    return size;
}

//m_parseStates.push_front(parseState);
static void CList_PushFront( ParseState ** ppList, const ParseState * pElem )
{
    ParseState * pCopy = xmalloc(sizeof(ParseState));

    *pCopy = *pElem;
    pCopy->pNext = *ppList;

    *ppList = pCopy;
}

//m_parseStates.pop_front();
static void CList_PopFront( ParseState ** ppList )
{
    ParseState * pElem;

    pElem = *ppList;
    *ppList = pElem->pNext;

    xfree(pElem);
}

// ------------------------------------------------------------------------------
// Vector implementation (parser state)
//
static void CVector_Init( CVector * pVec, uint elemSize )
{
    pVec->pVec = NULL;
    pVec->elemSize = elemSize;
    pVec->elemCount = 0;
}

static void CVector_Destroy( CVector * pVec )
{
    if (pVec->pVec != NULL)
    {
        xfree(pVec->pVec);
        pVec->pVec = NULL;
    }
    pVec->elemCount = 0;
}

static void CVector_Resize( CVector * pVec, uint elemCount )
{
    void * pNew;

    if (elemCount > pVec->elemCount)
    {
        pNew = xmalloc(elemCount * pVec->elemSize);
        memset(pNew, 0, elemCount * pVec->elemSize);
        if (pVec->pVec != NULL)
        {
            memcpy(pNew, pVec->pVec, pVec->elemCount * pVec->elemSize);
            xfree(pVec->pVec);
        }
        pVec->elemCount = elemCount;
        pVec->pVec = pNew;
    }
}

/*
static void * CVector_Get( CVector * pVec )
{
    return pVec->pVec;
}
*/

static ulong * CVector_GetUlong( CVector * pVec )
{
    return (ulong *) pVec->pVec;
}


// ------------------------------------------------------------------------------

enum
{
    DEBUG_OUT_NONE      = 0,
    DEBUG_OUT_ERROR     = 1,
    DEBUG_OUT_REPORT    = 2,
    DEBUG_OUT_EXPECT    = 3,
};

static void DebugOut(int level, const char* message );
#if 0
static void DebugOut(int level, const char* message, bool appendExpect );
static void DebugOut(int level, ParseError *error, bool appendExpect );
#endif
static void ParseStateDestroyProc( ParseState * pParseState )
{
    CVector_Destroy(&pParseState->openedTagParamCounts);
}

void HCParser_Init(const CParseTag* tagList)
{
    m_debugOutLevel = DEBUG_OUT_NONE;
    m_reportContext = NULL;
    m_tagListEntry  = tagList;

    CList_Init(&m_parseStates);
}

void HCParser_Destroy( void )
{
    ParseErrorClear();
    CList_Destroy(&m_parseStates, ParseStateDestroyProc);
}

bool HCParser_ParseLocalFile(const char* filename, void* reportContext)
{
    return HCParser_ParseFile(filename, reportContext, TRUE);
}

bool HCParser_ParseFile(const char* filename, void* reportContext, bool localFile)
{
    FILE* fp = localFile ? HCParser_OpenLocalFile(filename) : fopen(filename, "r");
    bool success;

    if (fp == NULL)
    {
        char * pMsg = xmalloc(strlen(filename) + 200);
        sprintf(pMsg, "Unable to open file '%s' for reading", filename);
        ParseErrorClear();
        ParseErrorAppend(PARSE_ERROR_FILE, pMsg);
        xfree(pMsg);
        return FALSE;
    }

    success = HCParser_ParseFd(fp, reportContext);
    fclose(fp);

    return success;
}

bool HCParser_ParseFd(FILE* fp, void* reportContext)
{
    bool success;

    ParseErrorClear();
    m_reportContext = reportContext;
    m_lineNumber    = 0;
    m_linePoint     = NULL;
    m_newlineChar   = 0;

    m_bufferPosition = m_bufferLength = MAX_READ_BUFFER;

    success = ProcessStream(fp);
    if (!success)
    {
#ifndef DPRINTF_OFF
        //DebugOut(DEBUG_OUT_ERROR, "\n\n");
        //DebugOut(DEBUG_OUT_ERROR, m_parseError);
#endif
    }

#ifndef DPRINTF_OFF
    //DebugOut(DEBUG_OUT_ERROR, "\n");
#endif
    return success;
}

FILE* HCParser_OpenLocalFile(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (fp == NULL)
    {
        {
            char * buffer = xmalloc(MAX_PATH + strlen(filename));

            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            strcpy(strrchr(buffer, '\\')+1, filename);

            fp = fopen(buffer, "r");
            xfree(buffer);
        }
    }
    return fp;
}

char * HCParser_GetError( void )
{
    return m_parseError.pMsg;
}

int HCParser_Str2Int(const char* text)
{
    const char* c = text;
    int n = 0;
    bool negative = FALSE;

    if (*c == '-')
    {
        negative = TRUE;
        c++;
    }

    if (*c == '0' && *++c == 'x')
    {
        while (*++c != '\0')
        {
            n *= 0x10;

            if (*c >= '0' && *c <= '9')
            {
                n += *c - '0';
            }
            else if (*c >= 'a' && *c <= 'f')
            {
                n += 0xA + *c - 'a';
            }
            else if (*c >= 'A' && *c <= 'F')
            {
                n += 0xA + *c - 'A';
            }
            else
            {
                return 0;
            }
        }
    }
    else
    {
        for ( ; *c != '\0'; c++)
        {
            if (*c >= '0' && *c <= '9')
            {
                n *= 10;
                n += *c - '0';
            }
            else
            {
                return 0;
            }
        }
    }

    return negative ? -n : n;
}

static void TrimLeft( void )
{
    assert(m_linePoint != NULL);
    for ( ; IsSpace(*m_linePoint); m_linePoint++) ;
}

static bool IsSpace(int c)
{
    return c == ' ' || c == '\t';
}

static bool IsAlpha(int c)
{
    return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z'));
}

static bool IsAlnum(int c)
{
    return IsAlpha(c) || IsDigit(c) || c == '_' || c == '-';
}

static bool IsDigit(int c)
{
    return c >= '0' && c <= '9';
}

static bool IsHex(int c)
{
    return IsDigit(c) || ((c >= 'A') && (c <= 'F')) || ((c >= 'a') && (c <= 'f'));
}

static bool IsDelim(int c)
{
    return c == ',' || c == ')' || c == '(' || c == '{' || c == '}' || c == '=';
}

static void ParseErrorAppend(PARSE_ERR_TYPE type, const char * pMsg)
{
    uint off;

    if (m_parseError.pMsg != NULL)
    {
        return;
    }

    m_parseError.pMsg = xmalloc(strlen(pMsg) + 200);
    m_parseError.type = type;

    if (type == PARSE_ERROR_LINE || type == PARSE_ERROR_POINT)
    {
        off = sprintf(m_parseError.pMsg, "Error on line %ld", m_lineNumber);

        if (type == PARSE_ERROR_POINT && m_linePoint != NULL)
        {
            sprintf(m_parseError.pMsg + off, " character %ld", (long)(m_linePoint - m_lineBuffer)+1);
        }
    }
    else
    {
        strcpy(m_parseError.pMsg, "Error");
    }
    strcat(m_parseError.pMsg, ": ");
    strcat(m_parseError.pMsg, pMsg);
}

static long ReadLineIntoBuffer(FILE* fp)
{
    *m_lineBuffer = '\0';
    m_linePoint = m_lineBuffer;

    while (m_bufferLength > 0)
    {
        for ( ; m_bufferPosition < m_bufferLength; m_bufferPosition++)
        {
            if (m_readBuffer[m_bufferPosition] == '\n' || m_readBuffer[m_bufferPosition] == '\r')
            {
                if (m_newlineChar == 0)
                {
                    m_newlineChar = m_readBuffer[m_bufferPosition];
                }
                if (m_readBuffer[m_bufferPosition] == m_newlineChar)
                {
                    m_lineNumber++;
                }

                // Stop if there is something in the buffer.
                if (*m_lineBuffer != '\0')
                {
                    m_bufferPosition++;
                    *m_linePoint = '\0';
                    return (long)(m_linePoint - m_lineBuffer);
                }
                continue;
            }
            if (m_linePoint >= m_lineBuffer + MAX_LINE_LENGTH-1)
            {
                ParseErrorAppend(PARSE_ERROR_LINE, "Line is too long");
                return -1;
            }

            *m_linePoint++ = m_readBuffer[m_bufferPosition];
        }

        if (m_bufferLength != MAX_READ_BUFFER)
        {
            break;
        }

        m_bufferLength = fread(m_readBuffer, 1, MAX_READ_BUFFER, fp);
        if (m_bufferLength != MAX_READ_BUFFER && !feof(fp))
        {
            ParseErrorAppend(PARSE_ERROR_FILE, "File I/O error while reading");
            return -1;
        }

        m_bufferPosition = 0;
    }

    if (*m_lineBuffer != '\0')
    {
        m_bufferLength = 0;
        *m_linePoint = '\0';
        return (long)(m_linePoint - m_lineBuffer);
    }
    return 0;
}

static bool ProcessStream(FILE* fp)
{
    InitializeParseState();

    while (1)
    {
        // Read one line into a buffer
        long length = ReadLineIntoBuffer(fp);
        if (length == -1)
        {
            return FALSE;
        }
        if (length == 0)
        {
            break;
        }

        m_linePoint = m_lineBuffer;
        TrimLeft();

        if (*m_linePoint == ';' || *m_linePoint == '#')
        {
            continue;
        }
        if (CList_GetFront(m_parseStates)->expect & EXPECT_SECTION && *m_linePoint == '[')
        {
            if (!ProcessSection())
            {
                return FALSE;
            }
        }
        if (!ProcessLine())
        {
            return FALSE;
        }
    }

    if (CList_GetFront(m_parseStates)->expect & (EXPECT_CLOSE_V|EXPECT_CLOSE_L))
    {
        if (CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_V)
        {
            ParseErrorAppend(PARSE_ERROR_LINE, "EOF while expecting ')'");
        }
        else
        {
            ParseErrorAppend(PARSE_ERROR_LINE, "EOF while expecting '}'");
        }
        return FALSE;
    }

    while (CList_GetSize(m_parseStates)> 1)
    {
        if (!CloseValue())
        {
            return FALSE;
        }
    }

    // The root state ensures there's at least one open tag at the end.
    if (!CloseTag(FALSE))
    {
        return FALSE;
    }
    return TRUE;
}

//static void DebugOutExp(int level, ParseError& error, bool appendExpect)
static void DebugOutExp(int level, const char * pMsg, bool appendExpect)
{
#if 0 //ndef DPRINTF_OFF
    if (level <= m_debugOutLevel)
    {
        ParseError pe;

        pe << error.str();

        if (appendExpect && m_debugOutLevel >= DEBUG_OUT_EXPECT)
        {
            pe << "[";
            AddExpectLine(pe, TRUE);
            pe << "]";
        }
        cout << pe.str();
    }
#endif
    dprintf1("%s\n", pMsg);
}

#if 0
static void DebugOutExp(int level, const char* message, bool appendExpect)
{
    DebugOutExp(level, ParseError() << message, appendExpect);
}
#endif

//static void DebugOut(int level, ParseError& error)
static void DebugOut(int level, const char * pMsg )
{
   DebugOutExp(level, pMsg, FALSE);
}

static bool ProcessLine( void )
{
    while (1)
    {
        TrimLeft();

        if (*m_linePoint == '\0' || *m_linePoint == ';' || *m_linePoint == '#')
        {
            break;
        }

        if ((CList_GetFront(m_parseStates)->expect & EXPECT_TAG) && IsAlpha(*m_linePoint))
        {
            if (ProcessTag())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_OPEN_L) && *m_linePoint == '{')
        {
            m_linePoint++;
            if (ProcessOpenTagListBracket())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_L) && *m_linePoint == '}')
        {
            m_linePoint++;
            if (ProcessCloseTagListBracket())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_OPEN_V) && *m_linePoint == '(')
        {
            m_linePoint++;
            if (ProcessOpenValueBracket())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_V) && *m_linePoint == ')')
        {
            m_linePoint++;
            if (ProcessCloseValueBracket())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_EQUAL) && *m_linePoint == '=')
        {
            m_linePoint++;
            if (ProcessEqual())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_NEXT_COMMA) && *m_linePoint == ',')
        {
            m_linePoint++;
            if (ProcessComma())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_COMMA) && *m_linePoint == ',')
        {
            m_linePoint++;
            if (ProcessComma())
            {
                continue;
            }
        }
        if ((CList_GetFront(m_parseStates)->expect & EXPECT_VALUE) && (IsAlnum(*m_linePoint) || *m_linePoint == '"'))
        {
            if (ProcessValue())
            {
                continue;
            }

            if (ParseErrorEmpty())
            {
                if (CList_GetFront(m_parseStates)->parseTags->parseTypes & PARSE_STRING)
                {
                    ParseErrorAppend(PARSE_ERROR_POINT, "Given value not a valid string");
                }
                if (CList_GetFront(m_parseStates)->parseTags->parseTypes & PARSE_NUMERIC)
                {
                    ParseErrorAppend(PARSE_ERROR_POINT, "Given value not a valid numeric");
                }
                if (CList_GetFront(m_parseStates)->parseTags->parseTypes & PARSE_CONSTANT)
                {
                    ParseErrorAppend(PARSE_ERROR_POINT, "Given value not a valid constant");
                }
                return FALSE;
            }
        }

        if (ParseErrorEmpty())
        {
            char msgBuf[200];
            if (*m_linePoint < 0x20)
            {
                sprintf(msgBuf, "Unexpected character 0x%02X, expecting ", (int)*m_linePoint);
            }
            else
            {
                sprintf(msgBuf, "Unexpected character '%c', expecting ", *m_linePoint);
            }

            //AddExpectLine(pe, FALSE);
            ParseErrorAppend(PARSE_ERROR_GENERIC, msgBuf);
        }
        return FALSE;
    }

#ifndef DPRINTF_OFF
    DebugOutExp(DEBUG_OUT_EXPECT, " EOL", TRUE);
#endif
    if (CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_EOL)
    {
        while (CList_GetFront(m_parseStates)->passEOL)
        {
            if (!CloseValue())
            {
                return FALSE;
            }
        }
        if (!CloseTag(FALSE))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static bool TagTakesValues(const CParseTag* parseTag)
{
    long i;

    if (parseTag->tagName == NULL)
    {
        return FALSE;
    }

    if (parseTag->parseTypes & PARSE_CHILDREN)
    {
        for (i = 0; parseTag->attributes.subTags[i].tagName != NULL; i++)
        {
            if (TagTakesValues(&parseTag->attributes.subTags[i]))
            {
                return TRUE;
            }
        }
        return FALSE;
    }
    return TRUE;
}

static long GetNextIterateValuesIndex( void )
{
    long paramIndex = CList_GetFront(m_parseStates)->paramIndex;
    const CParseTag* parseTags = CList_GetFront(m_parseStates)->parseTags;

    while (parseTags[++paramIndex].tagName != NULL)
    {
        if (TagTakesValues(&parseTags[paramIndex]))
        {
            break;
        }
    }

    if (parseTags[paramIndex].tagName == NULL)
    {
        return -1;
    }

    return paramIndex;
}

#if 0
static void AddExpectLine(ParseError& pe, bool debugging)
{
    const char* namesReadable[EXPECT_MAX] = { "", "tag-name",
        "value", "'='", "','", "'('", "')'", "'{'", "'}'", "EOL", "','" };
    const char* namesDebug[EXPECT_MAX] = { "se", "ta", "va", "eq",
        "co", "ov", "cv", "ol", "cl", "ec", "ce" };

    const char** names = debugging ? namesDebug : namesReadable;
    const char* comma = debugging ? "," : ", ";
    const char* lastComma = debugging ? "," : " or ";

    unsigned short expect = CList_GetFront(m_parseStates)->expect;
    int last = -1;
    int count = 0;

    for (int i = 0; i < EXPECT_MAX; i++)
    {
        if (*(names[i]) != '\0' && expect & (1 << i))
        {
            if (last != -1)
            {
                if (count++)
                {
                    pe << comma;
                }
                pe << names[last];
            }
            last = i;
        }
    }

    if (last != -1)
    {
        if (count++)
        {
            pe << lastComma;
        }
        pe << names[last];
    }
    else if (!debugging)
    {
        pe << " end-of-file";
    }
}
#endif

static bool ProcessSection( void )
{
    char* parseStart;

    while (CList_GetSize(m_parseStates)> 2)
    {
        if (!CloseValue())
        {
            return FALSE;
        }
    }
    if (CList_GetFront(m_parseStates)->paramIndex != -1)
    {
        if (!CloseTag(FALSE))
        {
            return FALSE;
        }
    }
    if (!OpenTag(0) || !OpenValue(FALSE))
    {
        return FALSE;
    }

    parseStart = ++m_linePoint;
    for ( ; *m_linePoint != '\0' && *m_linePoint != ']'; m_linePoint++) ;

    if (*m_linePoint == '\0')
    {
        ParseErrorAppend(PARSE_ERROR_POINT, "End of line before ']'");
        return FALSE;
    }

    while (*(m_linePoint+1) != '\0')
    {
        char* lastPoint = m_linePoint;
        for (m_linePoint++; IsSpace(*m_linePoint); m_linePoint++) ;

        if (*m_linePoint == '\0')
        {
            m_linePoint = lastPoint;
            break;
        }

        for ( ; *m_linePoint != '\0' && *m_linePoint != ']'; m_linePoint++) ;

        if (*m_linePoint == '\0')
        {
            ParseErrorAppend(PARSE_ERROR_POINT, "Trailing garbage after ']'");
            return FALSE;
        }
    }

    // Don't progress past this point so that this the '\0' we add here
    // will be read again and this whole line will be taken as finished.
    *m_linePoint = '\0';

    if (!AcceptValue(CList_GetFront(m_parseStates)->parseTags,
                     CList_GetFront(m_parseStates)->parseTags->parseTypes,
                     parseStart, (unsigned long)(m_linePoint - parseStart)))
    {
        return FALSE;
    }

    while (CList_GetSize(m_parseStates)> 2)
    {
        if (!CloseValue())
        {
            return FALSE;
        }
    }

    CList_GetFront(m_parseStates)->expect = EXPECT_TAG|EXPECT_SECTION;
    return TRUE;
}

static bool ProcessTag( void )
{
    char* parseStart = m_linePoint;
    char delim;
    const CParseTag* parseTag;

    for (m_linePoint++; IsAlnum(*m_linePoint); m_linePoint++) ;

    if (!IsSpace(*m_linePoint) && !IsDelim(*m_linePoint) && *m_linePoint != '\0')
    {
        return FALSE;
    }

    delim = *m_linePoint;
    *m_linePoint = '\0';

    if (CList_GetFront(m_parseStates)->paramIndex != -1)
    {
        if (!OpenTagList(FALSE))
        {
            return FALSE;
        }
    }

    while (1)
    {
        parseTag = CList_GetFront(m_parseStates)->parseTags;
        for ( ; parseTag->tagName != NULL; parseTag++)
        {
            if (_stricmp(parseTag->tagName, parseStart) == 0)
            {
                break;
            }
        }

        if (parseTag->tagName != NULL || CList_GetFront(m_parseStates)->bracketOpened ||
            CList_GetSize(m_parseStates)== 2)
        {
            break;
        }

        if (!CloseValue() || !CloseTag(FALSE))
        {
            return FALSE;
        }
    }

    if (parseTag->tagName == NULL)
    {
        char msgBuf[200];
        char* p;

        // unrecognized tag
        for (p = parseStart; *p != '\0'; p++)
        {
            if (*p < 0x20)
            {
                *p = '_';
            }
        }

        sprintf(msgBuf, "Unrecognized tag '%.50s'", parseStart);
        ParseErrorAppend(PARSE_ERROR_POINT, msgBuf);
        return FALSE;
    }
    *m_linePoint = delim;

#ifndef DPRINTF_OFF
    DebugOut(DEBUG_OUT_REPORT, "\n");
#endif

    if (!ReportTag(parseTag))
    {
        return FALSE;
    }
    return OpenTag((long)(parseTag - CList_GetFront(m_parseStates)->parseTags));
}

static bool ProcessOpenTagListBracket( void )
{
#ifndef DPRINTF_OFF
    DebugOut(DEBUG_OUT_EXPECT, " {");
#endif
    return OpenTagList(TRUE);
}

static bool ProcessCloseTagListBracket( void )
{
#ifndef DPRINTF_OFF
    DebugOut(DEBUG_OUT_EXPECT, " }");
#endif
    while (!CList_GetFront(m_parseStates)->bracketOpened)
    {
        if (!CloseValue())
        {
            return FALSE;
        }
    }
    return CloseValue() && CloseTag(FALSE);
}

static bool ProcessOpenValueBracket( void )
{
#ifndef DPRINTF_OFF
    DebugOut(DEBUG_OUT_EXPECT, " (");
#endif
    return OpenValue(TRUE);
}

static bool ProcessCloseValueBracket( void )
{
    DebugOut(DEBUG_OUT_EXPECT, " )");
    while (!CList_GetFront(m_parseStates)->bracketOpened)
    {
        if (!CloseValue())
        {
            return FALSE;
        }
    }

    return CloseValue();
}

static bool ProcessComma( void )
{
    DebugOut(DEBUG_OUT_EXPECT, ",");
    while (1)
    {
        if (CList_GetFront(m_parseStates)->paramIndex != -1)
        {
            if (CList_GetFront(m_parseStates)->iterateValues)
            {
                long nextIndex = GetNextIterateValuesIndex();
                if (nextIndex != -1)
                {
                    return OpenTag(nextIndex);
                }
                if (!CloseTag(FALSE))
                {
                    return FALSE;
                }
            }
            else
            {
                return CloseTag(FALSE);
            }
        }

        if (!CloseValue())
        {
            return FALSE;
        }
    }
    return TRUE;
}

static bool ProcessEqual( void )
{
    DebugOut(DEBUG_OUT_EXPECT, " =");

    if (CList_GetFront(m_parseStates)->expect & EXPECT_OPEN_V)
    {
        CList_GetFront(m_parseStates)->expect &= ~EXPECT_TAG;
        CList_GetFront(m_parseStates)->expect |= EXPECT_VALUE;
    }
    else
    {
        CList_GetFront(m_parseStates)->expect |= EXPECT_TAG;
    }

    CList_GetFront(m_parseStates)->expect &= ~EXPECT_EQUAL;
    CList_GetFront(m_parseStates)->expect |= EXPECT_CLOSE_EOL|EXPECT_CLOSE_COMMA;
    DebugOutExp(DEBUG_OUT_EXPECT, "", TRUE);
    return TRUE;
}

static bool ProcessValue( void )
{
    DebugOut(DEBUG_OUT_EXPECT, " Value");

    if (CList_GetFront(m_parseStates)->paramIndex != -1)
    {
        if (!OpenValue(FALSE))
        {
            return FALSE;
        }
    }

    if (!(CList_GetFront(m_parseStates)->expect & EXPECT_VALUE))
    {
        return FALSE;
    }
    CList_GetFront(m_parseStates)->expect &= ~EXPECT_VALUE;

    if (*m_linePoint == '"')
    {
        if (CList_GetFront(m_parseStates)->parseTags->parseTypes & PARSE_STRING)
        {
            char* parseStart = ++m_linePoint;
            for ( ; *m_linePoint != '\0' && *m_linePoint != '"'; m_linePoint++) ;

            if (*m_linePoint == '\0')
            {
                ParseErrorAppend(PARSE_ERROR_POINT, "End of line before closing '\"'");
                return FALSE;
            }

            // Overwrite the closing double-quotes.
            *m_linePoint++ = '\0';

            return AcceptValue(CList_GetFront(m_parseStates)->parseTags, PARSE_STRING,
                               parseStart, (unsigned long)(m_linePoint - parseStart));
        }
    }
    else if (IsAlnum(*m_linePoint))
    {
        if (CList_GetFront(m_parseStates)->parseTags->parseTypes & (PARSE_CONSTANT|PARSE_NUMERIC))
        {
            char* parseStart = m_linePoint;
            char delim;

            for ( ; IsAlnum(*m_linePoint); m_linePoint++) ;

            delim = *m_linePoint;
            *m_linePoint = '\0';

            if (!AcceptValue(CList_GetFront(m_parseStates)->parseTags,
                CList_GetFront(m_parseStates)->parseTags->parseTypes & (PARSE_CONSTANT|PARSE_NUMERIC),
                parseStart, (unsigned long)(m_linePoint - parseStart)))
            {
                return FALSE;
            }

            *m_linePoint = delim;
            return TRUE;
        }
    }

    return FALSE;
}

bool AcceptValue(const CParseTag* parseTag, unsigned char types,
                 char* value, unsigned long length)
{
    CParseValue parse_value;
    assert(types & (PARSE_STRING|PARSE_CONSTANT|PARSE_NUMERIC));

    if (length > CList_GetFront(m_parseStates)->parseTags->maxParseLength)
    {
        char msgBuf[200];
        sprintf(msgBuf, "Value length of '%.50s' = %ld is longer than limit %ld",
                        parseTag->tagName, length, parseTag->maxParseLength);
        ParseErrorAppend(PARSE_ERROR_POINT, msgBuf);
        return FALSE;
    }

    if (types & PARSE_CONSTANT && parseTag->attributes.constants != NULL)
    {
        const CParseConstant* pc = parseTag->attributes.constants;
        for ( ; pc->constant != NULL; pc++)
        {
            if (_stricmp(pc->constant, value) == 0)
            {
                break;
            }
        }

        if (pc->constant != NULL)
        {
            return ReportValue(parseTag, PARSE_CONSTANT, &pc->value, REPORT_VALUE);
        }
    }
    if (types & PARSE_NUMERIC)
    {
        char* c = value;
        if (*c == '-')
        {
            c++;
        }

        if (*c == '0' && *++c == 'x')
        {
            if (*++c != '\0')
            {
                while (IsHex(*++c)) ;
            }
            else
            {
                c--;
            }
        }
        else
        {
            for ( ; IsDigit(*c); c++) ;
        }

        if (*c == '\0')
        {
            CParseValue_AssignString(&parse_value, value);
            return ReportValue(parseTag, PARSE_NUMERIC, &parse_value, REPORT_VALUE);
        }
    }
    if (types & PARSE_STRING)
    {
        CParseValue_AssignString(&parse_value, value);
        return ReportValue(parseTag, PARSE_STRING, &parse_value, REPORT_VALUE);
    }

    return FALSE;
}

static void InitializeParseState( void )
{
    ParseState parseState;

    CList_Destroy(&m_parseStates, ParseStateDestroyProc);

    memset(m_rootParent, 0, sizeof(m_rootParent));
    m_rootParent[0].tagName             = "";
    m_rootParent[0].parseTypes          = PARSE_CHILDREN;
    m_rootParent[0].maxParseLength      = 1;
    m_rootParent[0].attributes.subTags  = m_tagListEntry;

    parseState.paramIndex = -1;
    parseState.bracketOpened = FALSE;
    parseState.iterateValues = FALSE;
    parseState.parseTags = m_rootParent;
    parseState.expect = 0;
    parseState.paramCounts = NULL;
    parseState.valueOpened = FALSE;
    CVector_Init(&parseState.openedTagParamCounts, sizeof(ulong));
    CList_PushFront(&m_parseStates, &parseState);

    OpenTag(0);
    OpenTagList(FALSE);
}

static bool OpenTag(long tagIndex)
{
    bool iterateValues;
    const CParseTag* parseTag;
    unsigned long paramCount;

    if (CList_GetFront(m_parseStates)->paramIndex != -1)
    {
        if (!CloseTag(TRUE))
        {
            return FALSE;
        }
    }

    CList_GetFront(m_parseStates)->paramIndex = tagIndex;
    CList_GetFront(m_parseStates)->expect &= ~EXPECT_TAG;
    iterateValues = CList_GetFront(m_parseStates)->iterateValues;

    if (CList_GetFront(m_parseStates)->parseTags[tagIndex].parseTypes & PARSE_CHILDREN)
    {
        CList_GetFront(m_parseStates)->expect |= EXPECT_OPEN_L|(iterateValues ? 0 : EXPECT_TAG);

        // Count the number of sub tags
        parseTag = CList_GetFront(m_parseStates)->parseTags[tagIndex].attributes.subTags;
        assert(parseTag != NULL);
        for (paramCount = 0; parseTag->tagName != NULL; parseTag++, paramCount++) ;

        // Initialize the number of values parsed for every sub tag
        CVector_Resize( &CList_GetFront(m_parseStates)->openedTagParamCounts, paramCount );
    }

    CList_GetFront(m_parseStates)->expect |= EXPECT_OPEN_V|(iterateValues ? EXPECT_VALUE : EXPECT_EQUAL);

    if (iterateValues)
    {
        if (GetNextIterateValuesIndex() != -1)
        {
            CList_GetFront(m_parseStates)->expect |= EXPECT_NEXT_COMMA;
        }
    }
    else
    {
        CList_GetFront(m_parseStates)->expect |= EXPECT_NEXT_COMMA;
    }

    DebugOutExp(DEBUG_OUT_EXPECT, ":OT", TRUE);
    return TRUE;
}

static bool CloseTag(bool openNext)
{
    long paramIndex;
    const CParseTag* parseTag;
    long i = 0;

    if (CList_GetFront(m_parseStates)->valueOpened)
    {
        CList_GetFront(m_parseStates)->valueOpened = FALSE;

        assert(CList_GetFront(m_parseStates)->paramIndex != -1);
        paramIndex = CList_GetFront(m_parseStates)->paramIndex;
        parseTag = &CList_GetFront(m_parseStates)->parseTags[paramIndex];

        if (parseTag->parseTypes & PARSE_CHILDREN)
        {
            for (i = 0; parseTag->attributes.subTags[i].tagName != NULL; i++)
            {
                if (CVector_GetUlong(&CList_GetFront(m_parseStates)->openedTagParamCounts)[i] < parseTag->attributes.subTags[i].minimumNumber)
                {
                    char msgBuf[200];
                    sprintf(msgBuf, "Number of '%.50s' = %ld is less than limit (%ld)",
                                    parseTag->attributes.subTags[i].tagName,
                                    CVector_GetUlong(&CList_GetFront(m_parseStates)->openedTagParamCounts)[i],
                                    parseTag->attributes.subTags[i].minimumNumber);
                    ParseErrorAppend(PARSE_ERROR_LINE, msgBuf);
                    return FALSE;
                }
            }
        }

        if (!ReportClose(parseTag))
        {
            return FALSE;
        }
    }

    CVector_Destroy( &CList_GetFront(m_parseStates)->openedTagParamCounts);
    CList_GetFront(m_parseStates)->paramIndex = -1;
    CList_GetFront(m_parseStates)->expect &= ~(EXPECT_NEXT_COMMA|EXPECT_VALUE|EXPECT_OPEN_V);

    if (!openNext)
    {
        CList_GetFront(m_parseStates)->expect &= ~(EXPECT_CLOSE_COMMA|EXPECT_CLOSE_EOL|EXPECT_OPEN_L|EXPECT_EQUAL);
        CList_GetFront(m_parseStates)->expect |= EXPECT_TAG;
        
        if (!(CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_L))
        {
            CList_GetFront(m_parseStates)->expect |= EXPECT_SECTION;
        }
    }

    DebugOutExp(DEBUG_OUT_EXPECT, ":CT", TRUE);
    return TRUE;
}

static bool OpenValue(bool withBracket)
{
    long paramIndex;
    const CParseTag* parseTag;
    unsigned long maxValuesCount;
    ParseState parseState;

    assert(CList_GetFront(m_parseStates)->paramIndex != -1);
    paramIndex = CList_GetFront(m_parseStates)->paramIndex;
    parseTag = &CList_GetFront(m_parseStates)->parseTags[paramIndex];

    // Make sure another value can be accepted
    maxValuesCount = 1;
    if (parseTag->parseTypes & PARSE_CHILDREN)
    {
        maxValuesCount = parseTag->maxParseLength;
    }

    if (++CVector_GetUlong(CList_GetFront(m_parseStates)->paramCounts)[paramIndex] > maxValuesCount)
    {
        char msgBuf[200];
        sprintf(msgBuf, "Number of values for '%.50s' has exceeded its limit (%ld)",
                        parseTag->tagName, maxValuesCount);
        ParseErrorAppend(PARSE_ERROR_POINT, msgBuf);
        return FALSE;
    }

    if (!CList_GetFront(m_parseStates)->valueOpened && !ReportOpen(parseTag))
    {
        return FALSE;
    }
    CList_GetFront(m_parseStates)->valueOpened = TRUE;
    CList_GetFront(m_parseStates)->expect &= ~(EXPECT_OPEN_V|EXPECT_VALUE|EXPECT_EQUAL);

    parseState.paramIndex = -1;
    parseState.paramCounts = &CList_GetFront(m_parseStates)->openedTagParamCounts;
    parseState.passEOL = !withBracket && (CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_EOL);
    parseState.valueOpened = FALSE;
    CVector_Init(&parseState.openedTagParamCounts, sizeof(ulong));

    if (withBracket)
    {
        parseState.bracketOpened = TRUE;
        parseState.expect = EXPECT_CLOSE_V;
    }
    else
    {
        parseState.bracketOpened = FALSE;
        parseState.expect = CList_GetFront(m_parseStates)->expect & (EXPECT_CLOSE_V|
            EXPECT_CLOSE_L|EXPECT_CLOSE_EOL|EXPECT_NEXT_COMMA|EXPECT_CLOSE_COMMA);
    }

    if (parseTag->parseTypes & PARSE_CHILDREN)
    {
        long firstIndex;

        parseState.iterateValues = TRUE;
        parseState.parseTags = parseTag->attributes.subTags;
        CList_PushFront(&m_parseStates, &parseState);
        DebugOutExp(DEBUG_OUT_EXPECT, ":OV", TRUE);

        firstIndex = GetNextIterateValuesIndex();
        if (firstIndex != -1)
        {
            if (!OpenTag(firstIndex))
            {
                return FALSE;
            }
            if (!withBracket)
            {
                if (!OpenValue(FALSE))
                {
                    return FALSE;
                }
            }
        }
    }
    else
    {
        parseState.iterateValues = FALSE;
        parseState.parseTags = parseTag;
        parseState.expect |= EXPECT_VALUE;
        CList_PushFront(&m_parseStates, &parseState);
        DebugOutExp(DEBUG_OUT_EXPECT, ":OV", TRUE);
    }
    return TRUE;
}

static bool OpenTagList(bool withBracket)
{
    long paramIndex = CList_GetFront(m_parseStates)->paramIndex;
    const CParseTag* parseTag = &CList_GetFront(m_parseStates)->parseTags[paramIndex];
    ParseState parseState;

    assert(paramIndex != -1);
    assert(parseTag->parseTypes & PARSE_CHILDREN);

    if (CList_GetFront(m_parseStates)->expect & EXPECT_OPEN_V)
    {
        if (CList_GetFront(m_parseStates)->paramCounts != NULL)
        {
            if (++CVector_GetUlong(CList_GetFront(m_parseStates)->paramCounts)[paramIndex] > parseTag->maxParseLength)
            {
                char msgBuf[200];
                sprintf(msgBuf, "Number of values for '%.50s' has exceeded its limit (%ld)",
                                parseTag->tagName, parseTag->maxParseLength);
                ParseErrorAppend(PARSE_ERROR_POINT, msgBuf);
                return FALSE;
            }
        }
    }

    if (!CList_GetFront(m_parseStates)->valueOpened && !ReportOpen(parseTag))
    {
        return FALSE;
    }
    CList_GetFront(m_parseStates)->valueOpened = TRUE;
    CList_GetFront(m_parseStates)->expect &= ~(EXPECT_OPEN_L|EXPECT_OPEN_V|EXPECT_VALUE|EXPECT_EQUAL);

    parseState.paramIndex = -1;
    parseState.paramCounts = &CList_GetFront(m_parseStates)->openedTagParamCounts;
    parseState.passEOL = !withBracket && (CList_GetFront(m_parseStates)->expect & EXPECT_CLOSE_EOL);
    parseState.valueOpened = FALSE;
    CVector_Init(&parseState.openedTagParamCounts, sizeof(ulong));

    if (withBracket)
    {
        parseState.bracketOpened = TRUE;
        parseState.expect = EXPECT_CLOSE_L;
    }
    else
    {
        parseState.bracketOpened = FALSE;
        parseState.expect = CList_GetFront(m_parseStates)->expect & (EXPECT_CLOSE_L|EXPECT_CLOSE_EOL);
    }

    parseState.iterateValues = FALSE;
    parseState.parseTags = parseTag->attributes.subTags;
    parseState.expect |= EXPECT_TAG|(withBracket ? 0 : EXPECT_SECTION);
    CList_PushFront(&m_parseStates, &parseState);

    DebugOutExp(DEBUG_OUT_EXPECT, ":OL", TRUE);
    return TRUE;
}

static bool CloseValue( void )
{
    if (CList_GetFront(m_parseStates)->paramIndex != -1)
    {
        if (!CloseTag(FALSE))
        {
            return FALSE;
        }
    }
    CList_PopFront(&m_parseStates);

    if (!CList_GetFront(m_parseStates)->iterateValues)
    {
        long paramIndex = CList_GetFront(m_parseStates)->paramIndex;
        const CParseTag* parseTag = &CList_GetFront(m_parseStates)->parseTags[paramIndex];

        CList_GetFront(m_parseStates)->expect |= EXPECT_CLOSE_COMMA;

        if (parseTag->parseTypes & PARSE_CHILDREN)
        {
            if (CList_GetFront(m_parseStates)->expect & EXPECT_OPEN_L)
            {
                CList_GetFront(m_parseStates)->expect |= EXPECT_EQUAL;
            }
        }
        else
        {
            CList_GetFront(m_parseStates)->expect |= EXPECT_CLOSE_EOL;
        }
    }

    DebugOutExp(DEBUG_OUT_EXPECT, ":CV", TRUE);
    return TRUE;
}

static bool ReportTag(const CParseTag* parseTag)
{
    return ReportValue(parseTag, 0, NULL, REPORT_TAG);
}

static bool ReportOpen(const CParseTag* parseTag)
{
    if (*parseTag->tagName != '\0')
    {
        dprintf1("ReportOpen: tag %s\n", parseTag->tagName);
    }
    return ReportValue(parseTag, 0, NULL, REPORT_OPEN);
}

static bool ReportClose(const CParseTag* parseTag)
{
    if (*parseTag->tagName != '\0')
    {
        dprintf1("ReportClose: tag %s\n", parseTag->tagName);
    }
    return ReportValue(parseTag, 0, NULL, REPORT_CLOSE);
}

static bool ReportValue(const CParseTag* parseTag, unsigned char type,
                        const CParseValue* value, int report)
{
    ParseReadProc* readProc;

#ifndef DPRINTF_OFF
    if (value != NULL)
    {
        switch (type)
        {
        case PARSE_CONSTANT: dprintf0("ReportValue: Constant"); break;
        case PARSE_NUMERIC: dprintf0("ReportValue: Numeric"); break;
        case PARSE_STRING: dprintf0("ReportValue: String"); break;
        }

        if (CParseValue_GetString(value) != NULL)
        {
            dprintf1(" = '%s'\n", CParseValue_GetString(value));
        }
        else
        {
            dprintf1(" = %d\n", CParseValue_GetNumber(value));
        }
    }
#endif

    readProc = parseTag->readProc;

    if (readProc == PASS_TO_PARENT)
    {
        //list<ParseState>::iterator it = m_parseStates.begin();
        ParseState * it = CList_GetFront(m_parseStates);

        // The top level state's paramIndex can be -1 but there'll always
        // be a lower level state where paramIndex is not -1.  (If need be
        // the sentinel value.)
        if (it->paramIndex == -1)
        {
            //it++;
            it = CList_GetNext(it);
        }

        // Look at older states until a NULL or a good callback is found.
        // There is a sentinel value at the root (m_rootParent) so there's
        // no need to check 'it' against m_parseStates.end().
        for ( ; it->parseTags[it->paramIndex].readProc == PASS_TO_PARENT; it=CList_GetNext(it)) ;
        readProc = it->parseTags[it->paramIndex].readProc;
    }

    if (readProc != NULL)
    {
        char * pErrMsg = NULL;

        if ( readProc(report, parseTag, type, value, m_reportContext, &pErrMsg) == FALSE )
        {
            if (pErrMsg != NULL)
                ParseErrorAppend(PARSE_ERROR_POINT, pErrMsg);
            else
                ParseErrorAppend(PARSE_ERROR_POINT, "Failed to process value");
            return FALSE;
        }
    }
    return TRUE;
}


//////////////////////////////////////////////////////////////////////////
// CParseValue
//////////////////////////////////////////////////////////////////////////

void CParseValue_AssignString( CParseValue * parse_value, const char * value )
{
    parse_value->m_string = value;
    parse_value->m_number = 0;
}

/*
void CParseValue_AssignInt( CParseValue * parse_value, int value )
{
    parse_value->m_string = NULL;
    parse_value->m_number = value;
}
*/

const char * CParseValue_GetString( const CParseValue * parse_value )
{
    return parse_value->m_string;
}

int CParseValue_GetNumber( const CParseValue * parse_value )
{
    if (parse_value->m_string != NULL)
    {
        return HCParser_Str2Int(parse_value->m_string);
    }
    return parse_value->m_number;
}


//////////////////////////////////////////////////////////////////////////
// CParseConstant
//////////////////////////////////////////////////////////////////////////

void CParseConstant_Assign(CParseConstant * pConst, const char* constant, CParseValue value)
{
    pConst->constant  = constant;
    pConst->value     = value;
}


//////////////////////////////////////////////////////////////////////////
// CParseTag
//////////////////////////////////////////////////////////////////////////

void CParseTag_InitCParseTag(CParseTag * parse_tag,
                             const char* tagName, unsigned char parseTypes,
                             unsigned char minimumNumber,unsigned long maxLength,
                             CAttributes attributes, ParseReadProc* readProc)
{
    parse_tag->tagName           = tagName;
    parse_tag->parseTypes        = parseTypes;
    parse_tag->minimumNumber     = minimumNumber;
    parse_tag->maxParseLength    = maxLength;
    parse_tag->attributes        = attributes;
    parse_tag->readProc          = readProc;
}


//////////////////////////////////////////////////////////////////////////
// HCParser_ParseError
//////////////////////////////////////////////////////////////////////////

static void ParseErrorClear( void )
{
    if (m_parseError.pMsg != NULL)
    {
        xfree(m_parseError.pMsg);
        m_parseError.pMsg = NULL;
    }
}

static bool ParseErrorEmpty( void )
{
    return (m_parseError.pMsg == NULL);
}


