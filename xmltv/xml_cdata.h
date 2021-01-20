/*
 *  XMLTV character data processing and storage
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
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xml_cdata.h,v 1.7 2005/12/31 19:31:01 tom Exp tom $
 */

#ifndef __XMLTV_CDATA_H
#define __XMLTV_CDATA_H

// ----------------------------------------------------------------------------
// String buffer definition
//
typedef struct
{
   char         * pStrBuf;      // pointer to malloc'ed text buffer
   uint         sizeHint;       // hint for initial malloc size and later increments
   uint         size;           // max. text length, i.e. size of allocated buffer
   uint         skip;           // number of whitespace chars at text start
   uint         off;            // number of valid chars in buffer
   uint         lang;           // language code (passed through only - not maintained here)
} XML_STR_BUF;

#define XML_STR_BUF_GET_LEN(S)     ((S).off)
#define XML_STR_BUF_GET_STR_LEN(S) ((S).off - (S).skip)
#define XML_STR_BUF_GET_STR(S)     (((S).pStrBuf != NULL) \
                                    ? ((S).pStrBuf + (S).skip) \
                                    : (char*)&((S).pStrBuf))  // hack to obtain non-const nul-string ""

#define XML_STR_BUF_SET_LANG(S,L)  ((S).lang = (L))
#define XML_STR_BUF_GET_LANG(S)    ((S).lang)

// enables use of macros instead of function calls
#define XML_CDATA_INLINE

// ----------------------------------------------------------------------------
// Interface functions
//
void XmlCdata_Init( XML_STR_BUF * pBuf, uint sizeHint );
void XmlCdata_Free( XML_STR_BUF * pBuf );
void XmlCdata_Grow( XML_STR_BUF * pBuf, uint len );
void XmlCdata_Assign( XML_STR_BUF * pDestBuf, XML_STR_BUF * pSrcBuf );
void XmlCdata_TrimWhitespace( XML_STR_BUF * pBuf );
void XmlCdata_AppendParagraph( XML_STR_BUF * pBuf, bool insertTwo );
void XmlCdata_AppendRawNOINLINE( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendLatin1ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_2ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_5ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_7ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_8ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_9ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_10ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_15ToUtf8( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_2ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_5ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_7ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_8ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_9ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_10ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendIso8859_15ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
void XmlCdata_AppendUtf8ToLatin1( XML_STR_BUF * pBuf, const char * pStr, uint len );
bool XmlCdata_CheckLatin1( const char * pStr );
bool XmlCdata_CheckLatin1Name( const char * pStr, bool isNmToken );
bool XmlCdata_CheckUtf8( const char * pStr );
bool XmlCdata_CheckUtf8Name( const char * pStr, bool isNmtoken );

#ifdef XML_CDATA_INLINE
#define XmlCdata_AppendRaw(PBUF,PSTR,LEN) \
   do { \
      if ( ((PBUF)->pStrBuf == NULL) || \
           ((PBUF)->off + (LEN) + 1 > (PBUF)->size) ) \
      { \
         XmlCdata_Grow((PBUF), (LEN)); \
      } \
      \
      memcpy((PBUF)->pStrBuf + (PBUF)->off, (PSTR), (LEN)); \
      (PBUF)->off += (LEN); \
      (PBUF)->pStrBuf[(PBUF)->off] = 0; \
   } while(0)
#define XmlCdata_AppendString(PBUF,PSTR) \
   do { \
      uint tmp_len = strlen(PSTR); \
      XmlCdata_AppendRaw((PBUF),(PSTR),tmp_len); \
   } while(0)
#define XmlCdata_AssignOrAppend(DESTBUF,SRCBUF) \
   do { \
      if (((DESTBUF)->off == 0) && ((SRCBUF)->off > 128)) \
      { \
         XmlCdata_Assign((DESTBUF), (SRCBUF)); \
      } \
      else \
      { \
         XmlCdata_AppendRaw((DESTBUF), XML_STR_BUF_GET_STR(*(SRCBUF)), XML_STR_BUF_GET_STR_LEN(*(SRCBUF))); \
         (DESTBUF)->lang = (SRCBUF)->lang; \
      } \
   } while(0)
#define XmlCdata_Reset(PBUF) \
   do { \
      if ((PBUF)->pStrBuf != NULL) \
      { \
         (PBUF)->off = 0; \
         (PBUF)->skip = 0; \
         (PBUF)->pStrBuf[0] = 0; \
      } \
   } while(0)
#else
#define XmlCdata_AppendRaw(B,S,L) XmlCdata_AppendRawNOINLINE((B),(S),(L))
void XmlCdata_AppendString( XML_STR_BUF * pBuf, const char * pStr );
void XmlCdata_AssignOrAppend( XML_STR_BUF * pDestBuf, XML_STR_BUF * pSrcBuf );
void XmlCdata_Reset( XML_STR_BUF * pBuf );
#endif // XML_CDATA_INLINE

#endif // __XMLTV_CDATA_H
