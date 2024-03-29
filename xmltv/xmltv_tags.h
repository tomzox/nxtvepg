/*
 *  XMLTV element parser
 *
 *  Copyright (C) 2007-2011, 2020-2023 T. Zoerner
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
 *  Description: see C source file.
 */

#ifndef __XMLTV_TAGS_H
#define __XMLTV_TAGS_H

#include "epgdb/epg_lang.h"

#if defined (__cplusplus)
extern "C" {
#endif

typedef enum
{
   XML_ENC_ISO8859_1,
   XML_ENC_ISO8859_2,
   XML_ENC_ISO8859_5,
   XML_ENC_ISO8859_7,
   XML_ENC_ISO8859_8,
   XML_ENC_ISO8859_9,
   XML_ENC_ISO8859_10,
   XML_ENC_ISO8859_15,
   XML_ENC_UTF8,
   XML_ENC_UTF16BE,
   XML_ENC_UTF16LE,
   XML_ENC_UNKNOWN
} XML_ENCODING;

#define IS_XML_ENC_ISO8859(ENC) ((ENC) <= XML_ENC_ISO8859_15)

// bitfield with detection codes
typedef enum
{
   XMLTV_DETECTED_OK            = 0,         // no error
   XMLTV_DETECTED_UNSUP_ENC     = (1<<0),    // scanner found unsupported encoding
   XMLTV_DETECTED_XML           = (1<<1),    // <?xml...?> found
   XMLTV_DETECTED_UNSUP_XMLENC  = (1<<2),    // <?xml encoding="..."?> mismatch
   XMLTV_DETECTED_DOCTYPE       = (1<<3),    // <!DOCTYPE tv...> found
   XMLTV_DETECTED_NOT_TV        = (1<<4),    // wrong top-level tag found -> XML but not XMLTV
   XMLTV_DETECTED_SYNTAX        = (1<<5),    // syntax error found
} XMLTV_DETECTION;

#define XMLTV_DETECTED_OK(D) (((D) & ~(XMLTV_DETECTED_XML | XMLTV_DETECTED_DOCTYPE)) == 0)

// Interface to lexigraphical scanner (callback functions)
void XmltvTags_Open( const char * pTagName );
bool XmltvTags_Close( const char * pTagName );
void XmltvTags_Data( XML_STR_BUF * pBuf );
void XmltvTags_AttribIdentify( const char * pName );
void XmltvTags_AttribData( XML_STR_BUF * pBuf );
bool XmltvTags_AttribsComplete( void );
void Xmltv_SyntaxError( const char * pMsg, const char * pStr );
void Xmltv_ScanFatalError( const char * pMsg );
void XmltvTags_ScanUnsupEncoding( const char * pName );
const char * XmltvTags_TranslateErrorCode( XMLTV_DETECTION detection );

// Interface to prolog parser
void XmltvTags_Encoding( const char * pName );
void XmltvTags_XmlVersion( const char * pVersion );
void XmltvTags_DocType( const char * pName );
void XmltvTags_DocIntDtdClose( void );
void XmltvTags_PiTarget( const char * pName );
void XmltvTags_PiContent( const char * pValue );
void XmltvTags_Notation( int stepIdx, const char * pValue );
void XmltvTags_CheckName( const char * pStr );
void XmltvTags_CheckCharset( const char * pStr );
void XmltvTags_CheckNmtoken( const char * pStr );
void XmltvTags_CheckSystemLiteral( const char * pStr );

// Interface to main
void XmltvTags_StartScan( FILE * fp, bool load );
void XmlTags_ScanStop( void );
XMLTV_DETECTION XmltvTags_QueryDetection( void );
EPG_LANG_CODE XmltvTags_GetLanguage( void );

// Interface to xmltv.lex
void XmlScan_Init( void );
void XmlScan_Destroy( void );
void XmlScan_Stop( void );
bool XmlScan_SetEncoding( XML_ENCODING encoding );
void XmlScan_EntityDefName( const char * pName, int isParamEntity );
void XmlScan_EntityDefValue( const char * pValue );

#if defined (__cplusplus)
} // extern "C"
#endif
#endif  // __XMLTV_TAGS_H
