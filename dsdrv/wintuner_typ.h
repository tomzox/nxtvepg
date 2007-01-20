/////////////////////////////////////////////////////////////////////////////
// #Id: ParsingCommon.h,v 1.3 2004/12/01 22:01:18 atnak Exp #
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
// nxtvepg $Id: wintuner_typ.h,v 1.2 2006/01/05 14:22:36 tom Exp tom $
/////////////////////////////////////////////////////////////////////////////

#ifndef __WINTUNER_TYP_H
#define __WINTUNER_TYP_H

//
// TunerID parsing
//
// ParseTag:
//   { "Tuner", PARSE_NUMERIC|PARSE_CONSTANT, ..., 32, ..., k_parseTunerConstants, ... }
//
// Interpreter:
//   ReadTunerProc(..., ParseTunerInfo* tunerInfo)
//
//
// UseTDA9887 parsing
//
// ParseTag:
//   { "UseTDA9887", PARSE_CHILDREN, ..., 1, k_parseUseTDA9887, ..., ... }
//
// Interpreter:
//   ReadUseTDA9887Proc(..., ParseUseTDA9887Info* useTDA9887Info)
//

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

extern const CParseConstant k_parseTunerConstants[];
extern const CParseTag k_parseUseTDA9887[];


//////////////////////////////////////////////////////////////////////////
// Structures
//////////////////////////////////////////////////////////////////////////

typedef struct _ParseTunerInfo TParseTunerInfo;
typedef struct _ParseUseTDA9887Info TParseUseTDA9887Info;

struct _ParseTunerInfo
{
    eTunerId    tunerId;
};

struct _ParseUseTDA9887Info
{
    BOOL                    useTDA9887;
    TTDA9887Modes           tdaModes[TDA9887_FORMAT_LASTONE];
    eTDA9887Format          _readingFormat;
    TTDA9887Modes           _readingModes;
};


//////////////////////////////////////////////////////////////////////////
// Interpreters
//////////////////////////////////////////////////////////////////////////

BOOL ReadTunerProc(int report, const CParseTag* tag, unsigned char type,
                   const CParseValue* value, TParseTunerInfo* tunerInfo, char ** ppErrMsg);

BOOL ReadUseTDA9887Proc(int report, const CParseTag* tag, unsigned char type,
                        const CParseValue* value, TParseUseTDA9887Info* useTDA9887Info, char ** ppErrMsg);


#endif // __WINTUNER_TYP_H

