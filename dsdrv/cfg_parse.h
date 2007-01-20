/////////////////////////////////////////////////////////////////////////////
// #Id: HierarchicalConfigParser.h,v 1.13 2004/12/08 21:25:21 atnak Exp #
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
// nxtvepg $Id: cfg_parse.h,v 1.2 2006/01/05 14:22:36 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef __CFG_PARSE_H
#define __CFG_PARSE_H

// parseTypes for CParseTag
enum
{
    PARSE_STRING        = 1 << 0,
    PARSE_NUMERIC       = 1 << 1,
    PARSE_CONSTANT      = 1 << 2,
    PARSE_CHILDREN      = 1 << 3,

    PARSE_NUM_OR_CONST  = PARSE_NUMERIC|PARSE_CONSTANT,
};

// ParseReadProc reports
enum
{
    REPORT_TAG,
    REPORT_OPEN,
    REPORT_CLOSE,
    REPORT_VALUE,
};

typedef enum
{
    PARSE_ERROR_GENERIC,
    PARSE_ERROR_FILE,
    PARSE_ERROR_LINE,
    PARSE_ERROR_POINT,
} PARSE_ERR_TYPE;

struct CParseTag_struct;

typedef struct
{
    const char*     m_string;
    int             m_number;
} CParseValue;

typedef struct
{
    const char*     constant;
    CParseValue     value;
} CParseConstant;

typedef struct
{
    const struct CParseTag_struct*      subTags;
    const CParseConstant* constants;
} CAttributes;

#define PCINT(NAME,VAL) {NAME, {NULL, VAL}}
#define PCSTR(NAME,STR) {NAME, {STR, 0}}
#define PCEND           {NULL}

// Callback function type for CParseTag
typedef bool (ParseReadProc)(int report, const struct CParseTag_struct* tag, unsigned char type,
                             const CParseValue* value, void* context, char **ppErrMsg);

// This PASS_TO_PARENT value can be specified as the 'readProc' value of
// CParseTag to have to parent tag's callback function called.
#define PASS_TO_PARENT ((ParseReadProc*) 1L)

typedef struct CParseTag_struct
{
    // Name of the tag to be parsed.
    const char*             tagName;
    // Parsing mode for the tag.
    unsigned char           parseTypes;
    // Minimum number of the same type of tags that can be parsed.
    unsigned long           minimumNumber;
    // Maximum length in characters the value of the tag can be or
    // maximum tags of the same kind for PARSE_CHILDREN.
    unsigned long           maxParseLength;
    // Either a list of child tags or a list of constants.
    CAttributes             attributes;
    // The callback to be called when the tag is parsed.
    ParseReadProc*          readProc;
} CParseTag;

#define PTLEAF(NAME,TYPE,MIN,MAX,ATT,CB)  {NAME,TYPE,MIN,MAX,{NULL,NULL},CB}
#define PTCHILD(NAME,TYPE,MIN,MAX,ATT,CB) {NAME,TYPE,MIN,MAX,{ATT,NULL},CB}
#define PTCONST(NAME,TYPE,MIN,MAX,ATT,CB) {NAME,TYPE,MIN,MAX,{NULL,ATT},CB}
#define PTEND                             {NULL}

void CParseConstant_Assign(CParseConstant * pConst,
                           const char* constant, CParseValue value);
void CParseTag_InitCParseTag(CParseTag * parse_tag,
                             const char* tagName, unsigned char parseTypes,
                             unsigned char minimumNumber,unsigned long maxLength,
                             CAttributes attributes, ParseReadProc* readProc);

bool HCParser_ParseLocalFile(const char* filename, void* reportContext);
bool HCParser_ParseFile(const char* filename, void* reportContext, bool localFile);
bool HCParser_ParseFd(FILE* fp, void* reportContext);
FILE* HCParser_OpenLocalFile(const char* filename);
char * HCParser_GetError( void );
int HCParser_Str2Int(const char* text);
void HCParser_Init(const CParseTag* tagList);
void HCParser_Destroy( void );

const char * CParseValue_GetString( const CParseValue * parse_value );
int CParseValue_GetNumber( const CParseValue * parse_value );

#endif  // __CFG_PARSE_H
