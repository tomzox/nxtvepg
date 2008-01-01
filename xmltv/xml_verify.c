/*
 *  XML scanner/parser backend for verification
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
 *    This module replaces xmltv_tags.c during verification. Instead
 *    of processing the data, it's written out again in a "canonical"
 *    format which is defined by the XML testsuite.  This output is then
 *    compared with the expected output to validate the parser.
 *
 *    Note: currently not all valid test documents produce the expected
 *    result, also not all non-well-formed XML documents are reported as
 *    errors.  That's firstly because only a small part of the DTD is
 *    evaluated (most importantly, default attributes are not inserted);
 *    secondly, external entities are not included.  These features are
 *    not required here, since this parser supports only one DTD. It
 *    would hardly make sense to implement this support only to pass the
 *    testcases.  The following list details missing validation features.
 *
 *  TODO:
 *    - insert attribute defaults from attribute declaration
 *    - order attributes lexicographically in canonical output
 *    - report error if attribute occurs more than once in a tag
 *
 *  TODO scanner:
 *    - report error when entity replacement in content doesn't match
 *      "content" production (e.g. only opening tag inside entity)
 *    - strip whitespace from attribute values not declared as CDATA
 *
 *  Author: Tom Zoerner
 *
 *  $Id: xml_verify.c,v 1.5 2005/12/31 19:31:29 tom Exp tom $
 */

#define DEBUG_SWITCH DEBUG_SWITCH_XMLTV
#define DPRINTF_OFF

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#include "xmltv/xml_cdata.h"
#include "xmltv/xmltv_db.h"
#include "xmltv/xmltv_tags.h"

extern FILE *yyin;
extern int yyparse( void );
extern int yylex( void );
extern int yydebug;


// ----------------------------------------------------------------------------
// Internal parser stack
//
#define XML_STACK_MAX_DEPTH   20

typedef struct
{
   XML_STR_BUF  tagName;
} XML_PARSER_STACK;

typedef struct
{
   XML_PARSER_STACK tagStack[XML_STACK_MAX_DEPTH];
   XML_STR_BUF  docName;
   bool         haveDocDecl;
   bool         haveNotation;
   uint         stackDepth;
   uint         syntaxError;
} XML_PARSER_STATE;

static XML_PARSER_STATE xps;

// ----------------------------------------------------------------------------
// Print a character string, while replacing special chars with entities
// - replacements acc. to specification of canonical XML
//
static void XmltvTags_Print( const char * pText )
{
   char outbuf[256], *po;
   uint plen;
   uint outlen;

   po = outbuf;
   outlen = sizeof(outbuf);
   while (*pText != 0)
   {
      if (outlen < 7)
      {  // buffer is almost full -> flush it
         fwrite(outbuf, sizeof(char), sizeof(outbuf) - outlen, stdout);
         po = outbuf;
         outlen = sizeof(outbuf);
      }

      if (*pText == '<')
      {
         pText++;
         strcpy(po, "&lt;");
         po     += 4;
         outlen -= 4;
      }
      else if (*pText == '>')
      {
         pText++;
         strcpy(po, "&gt;");
         po     += 4;
         outlen -= 4;
      }
      else if (*pText == '&')
      {
         pText++;
         strcpy(po, "&amp;");
         po     += 5;
         outlen -= 5;
      }
      else if (*pText == '"')
      {
         pText++;
         strcpy(po, "&quot;");
         po     += 6;
         outlen -= 6;
      }
      else if ((*pText == '\n') || (*pText == '\r') || (*pText == '\t'))
      {
         plen = sprintf(po, "&#%d;", *pText);
         pText++;
         po     += plen;
         outlen -= plen;
      }
      else
      {
         *(po++) = *(pText++);
         outlen -= 1;
      }
   }

   // flush the output buffer
   if (outlen != sizeof(outbuf))
   {
      fwrite(outbuf, sizeof(char), sizeof(outbuf) - outlen, stdout);
   }
}

// ----------------------------------------------------------------------------
// Push new tag on parser stack
// 
void XmltvTags_Open( const char * pTagName )
{
   XmltvTags_CheckName(pTagName);

   if (xps.stackDepth < XML_STACK_MAX_DEPTH)
   {
      if (xps.stackDepth == 0)
      {
         // the name in the top-level tag must match the one given in <!DOCTYPE...>
         // the DOCTYPE element is optional, however
         if ( (XML_STR_BUF_GET_STR_LEN(xps.docName) > 0) &&
              (strcmp(XML_STR_BUF_GET_STR(xps.docName), pTagName) != 0) )
         {
            Xmltv_SyntaxError("XML toplevel tag doesn't match DOCTYPE", pTagName);
         }
      }
      XmlCdata_Reset(&xps.tagStack[xps.stackDepth].tagName);
      XmlCdata_AppendRaw(&xps.tagStack[xps.stackDepth].tagName, pTagName, strlen(pTagName));

      printf("<%s", pTagName);
      xps.stackDepth += 1;
   }
   else
   {  // abort: stack overflow (the current tag is discarded, which will cause more errors)
      Xmltv_SyntaxError("XML parser fatal error: tags nested too deply", pTagName);
   }
}

// ----------------------------------------------------------------------------
// Pop tag from parser stack
// - tag name can be NULL for empty tags (e.g. <stereo />)
// - returns FALSE if the document top-level tag is closed
//
bool XmltvTags_Close( const char * pTagName )
{
   if (xps.stackDepth > 0)
   {
      xps.stackDepth -= 1;

      if (pTagName == NULL)
      {
         printf(">");
      }
      else
      {
         if (strcmp(pTagName, XML_STR_BUF_GET_STR(xps.tagStack[xps.stackDepth].tagName)) != 0)
         {
            Xmltv_SyntaxError("Mismatching closing tag found", pTagName);
         }
      }
      printf("</%s>", XML_STR_BUF_GET_STR(xps.tagStack[xps.stackDepth].tagName));
   }
   else
   {
      Xmltv_SyntaxError("Cannot pop tag from empty stack", pTagName);
   }

   return (xps.stackDepth > 0);
}

// ----------------------------------------------------------------------------
// Processe PCDATA inbetween open and close tag
//
void XmltvTags_Data( XML_STR_BUF * pBuf )
{
   XmltvTags_CheckCharset(XML_STR_BUF_GET_STR(*pBuf));

   XmltvTags_Print(XML_STR_BUF_GET_STR(*pBuf));
}

// ----------------------------------------------------------------------------
// Notify that opening tag is closed, i.e. all attributes processed
// - when the callback returns FALSE, it's content and child elements are skipped;
//   this is an optimization for skipping tags which fall outside certain criteria
// - note this function is currently not called for empty tags, i.e. when the
//   has no childs nor content
//
bool XmltvTags_AttribsComplete( void )
{
   printf(">");
   return TRUE;
}

// ----------------------------------------------------------------------------
// Identify a tag's attribute by its name
//
void XmltvTags_AttribIdentify( const char * pName )
{
   printf(" %s=", pName);
}

// ----------------------------------------------------------------------------
// Process an attribute's data
// - data is already normalized by scanner according to XML 1.0 ch. 3.3.3
//   i.e. entity references are resolved and newline chars replaced by blank
// - TODO: for non-CDATA remove leading space and reduce multiple whitespace into one
//
void XmltvTags_AttribData( XML_STR_BUF * pBuf )
{
   XmltvTags_CheckCharset(XML_STR_BUF_GET_STR(*pBuf));

   printf("\"");
   XmltvTags_Print(XML_STR_BUF_GET_STR(*pBuf));
   printf("\"");
}

// ----------------------------------------------------------------------------
// Handle processing instructions
//
void XmltvTags_PiTarget( const char * pName )
{
   XmltvTags_CheckName(pName);

   if (strcasecmp(pName, "xml") == 0)
   {
      Xmltv_SyntaxError("Invalid PI target name", pName);
   }
   printf("<?%s ", pName);
}

void XmltvTags_PiContent( const char * pValue )
{
   XmltvTags_CheckCharset(pValue);

   if (pValue != NULL)
   {
      printf("%s", pValue);
   }
   printf("?>");
}

// ----------------------------------------------------------------------------
// Check for invalid characters in unused names or content
// - important: for validation against the XML testsuite you must choose UTF-8
//   encoding, else all non-Latin-1 characters are replaced with 0xA0
//
void XmltvTags_CheckName( const char * pStr )
{
#ifdef XMLTV_OUTPUT_UTF8
   if ( XmlCdata_CheckUtf8Name(pStr, FALSE) == FALSE )
#else
   if ( XmlCdata_CheckLatin1Name(pStr, TRUE) == FALSE )
#endif
   {
      Xmltv_SyntaxError("Invalid character in XML name", pStr);
   }
}

void XmltvTags_CheckCharset( const char * pStr )
{
#ifdef XMLTV_OUTPUT_UTF8
   if ( XmlCdata_CheckUtf8(pStr) == FALSE )
#else
   if ( XmlCdata_CheckLatin1(pStr) == FALSE )
#endif
   {
      Xmltv_SyntaxError("Invalid character in XML content or attribute data", pStr);
   }
}

void XmltvTags_CheckNmtoken( const char * pStr )
{
#ifdef XMLTV_OUTPUT_UTF8
   if ( XmlCdata_CheckUtf8Name(pStr, TRUE) == FALSE )
#else
   if ( XmlCdata_CheckLatin1Name(pStr, TRUE) == FALSE )
#endif
   {
      Xmltv_SyntaxError("Invalid character in XML name", pStr);
   }
}

void XmltvTags_CheckSystemLiteral( const char * pStr )
{
   XmltvTags_CheckCharset(pStr);
}

// ----------------------------------------------------------------------------
// Scanner callback when an unsupported encoding is encountered
//
void XmltvTags_ScanUnsupEncoding( const char * pName )
{
   Xmltv_SyntaxError("Unsupported encoding", pName);
}

// ----------------------------------------------------------------------------
// Process XML header <?xml version="..." encoding="..."?>
//
void XmltvTags_Encoding( const char * pName )
{
   int  page, nchar;
   bool result;

   if ( (strncasecmp(pName, "ascii", 5) == 0) ||
        (strncasecmp(pName, "us-ascii", 8) == 0) )
   {
      result = XmlScan_SetEncoding(XML_ENC_ISO8859_1);
   }
   else if ( (sscanf(pName, "%*[iI]%*[sS]%*[oO]8859%*[_-]%u%n", &page, &nchar) >= 1) ||
             (sscanf(pName, "%*[iI]%*[sS]%*[oO]8859%u%n", &page, &nchar) >= 1) ||
             (sscanf(pName, "%*[iI]%*[sS]%*[oO]%*[_-]8859%*[_-]%u%n", &page, &nchar) >= 1) ||
             (sscanf(pName, "%*[iI]%*[sS]%*[oO]%*[_-]8859%u%n", &page, &nchar) >= 1) ||
             (sscanf(pName, "%*[iI]%*[sS]%*[oO]8859%u%n", &page, &nchar) >= 1) ||
             (sscanf(pName, "8859%*[_-]%u%n", &page, &nchar) >= 1) )
   {
      switch (page)
      {
         case 1:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_1); break;
         case 2:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_2); break;
         case 5:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_5); break;
         case 7:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_7); break;
         case 8:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_8); break;
         case 9:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_9); break;
         case 10: result = XmlScan_SetEncoding(XML_ENC_ISO8859_10); break;
         case 15: result = XmlScan_SetEncoding(XML_ENC_ISO8859_15); break;
         default: result = FALSE; break;
      }
   }
   else if ( (sscanf(pName, "%*[lL]%*[aA]%*[tT]%*[iI]%*[nN]%*[_-]%u%n", &page, &nchar) >= 1) ||
             (sscanf(pName, "%*[lL]%*[aA]%*[tT]%*[iI]%*[nN]%u%n", &page, &nchar) >= 1) )
   {
      // note: "latin-X" codes are not 1:1 equivalent to ISO-8859 code pages
      switch (page)
      {
         case 1:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_1); break;   // Western Europe
         case 2:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_2); break;   // Eastern Europe
         case 5:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_9); break;   // Turkish
         case 6:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_10); break;  // Nordic
         case 9:  result = XmlScan_SetEncoding(XML_ENC_ISO8859_15); break;  // Western Europe, updated
         default: result = FALSE; break;
      }
   }
   else if (strcasecmp(pName, "latin-cyrillic") == 0)
   {
      result = XmlScan_SetEncoding(XML_ENC_ISO8859_5);
   }
   else if (strcasecmp(pName, "latin-greek") == 0)
   {
      result = XmlScan_SetEncoding(XML_ENC_ISO8859_7);
   }
   else if (strcasecmp(pName, "latin-hebrew") == 0)
   {
      result = XmlScan_SetEncoding(XML_ENC_ISO8859_8);
   }
   else if ( (strcasecmp(pName, "utf8") == 0) ||
             (strcasecmp(pName, "utf-8") == 0) )
   {
      result = XmlScan_SetEncoding(XML_ENC_UTF8);
   }
   else if ( (strcasecmp(pName, "utf16") == 0) ||
             (strcasecmp(pName, "utf-16") == 0) ||
             (strcasecmp(pName, "utf16be") == 0) ||
             (strcasecmp(pName, "utf-16be") == 0) )
   {
      result = XmlScan_SetEncoding(XML_ENC_UTF16BE);
   }
   else if ( (strcasecmp(pName, "utf16le") == 0) ||
             (strcasecmp(pName, "utf-16le") == 0) )
   {
      result = XmlScan_SetEncoding(XML_ENC_UTF16LE);
   }
   else
   {
      result = FALSE;
   }

   if (result == FALSE)
   {
      Xmltv_SyntaxError("Unsupported encoding or encoding mismatch in <?xml?>", pName);
   }
}

void XmltvTags_XmlVersion( const char * pVersion )
{
   if (strcmp(pVersion, "1.0") != 0)
   {
      Xmltv_SyntaxError("Incompatible XML version", pVersion);
   }
}

// ----------------------------------------------------------------------------
// Forward DOCTYPE declaration
//
void XmltvTags_DocType( const char * pName )
{
   XmlCdata_Reset(&xps.docName);
   XmlCdata_AppendRaw(&xps.docName, pName, strlen(pName));
   xps.haveDocDecl = TRUE;
}

void XmltvTags_DocIntDtdClose( void )
{
   if (xps.haveNotation)
   {
      printf("]>\n");
      xps.haveNotation = FALSE;
   }
}

// ----------------------------------------------------------------------------
// Forward NOTATION declaration from internal DTD
// - see invocations on xml_prolog.yy for an explanation of steps
//
void XmltvTags_Notation( int stepIdx, const char * pValue )
{
   if (xps.haveNotation == FALSE)
   {
      printf("<!DOCTYPE %s [\n", XML_STR_BUF_GET_STR(xps.docName));
   }
   xps.haveNotation = TRUE;

   switch(stepIdx)
   {
      case 0:
         printf("<!NOTATION %s ", pValue);
         break;
      case 1:
         XmltvTags_CheckSystemLiteral(pValue);
         printf("SYSTEM '%s'>\n", pValue);
         break;
      case 2:
         printf("PUBLIC '%s' ", pValue);
         break;
      case 3:
         printf("PUBLIC '%s'>\n", pValue);
         break;
      case 4:
         XmltvTags_CheckSystemLiteral(pValue);
         printf("'%s'>\n", pValue);
         break;
      default:
         SHOULD_NOT_BE_REACHED;
         break;
   }
}

// ----------------------------------------------------------------------------
// Notify about syntax error inside a tag
//
void Xmltv_SyntaxError( const char * pMsg, const char * pStr )
{
   fprintf(stderr, "XmltvTag-SyntaxError: %s: '%s'\n", pMsg, pStr);
   xps.syntaxError += 1;

   if (xps.syntaxError > 40)
   {
      fprintf(stderr, "Xmltv-SyntaxError: too many errors - aborting\n");
      XmlScan_Stop();
   }
}

// ----------------------------------------------------------------------------
// Function called by scanner upon I/O or malloc failures
// - ideally this function should raise an exception and not return, else
//   the scanner might run into a NULL pointer dereference
//
void Xmltv_ScanFatalError( const char * pMsg )
{
   debug1("Xmltv-ScanFatalError: %s", pMsg);
}

// ----------------------------------------------------------------------------
// Free Resources
//
static void XmltvTags_Destroy( void )
{
   uint  stackIdx;

   for (stackIdx = 0; stackIdx < XML_STACK_MAX_DEPTH; stackIdx++)
   {
      XmlCdata_Free(&xps.tagStack[stackIdx].tagName);
   }
   XmlCdata_Free(&xps.docName);
}

// ----------------------------------------------------------------------------
// Entry point
//
int main( int argc, char ** argv )
{
   FILE * fp;

   // initialize internal state
   memset(&xps, 0, sizeof(xps));

   XmlScan_Init();

   if (argc >= 2)
   {
#if DEBUG_SWITCH_XMLTV == ON
      if ((argc >= 3) && (strcmp(argv[2], "debug") == 0))
      {
         yydebug = 1;
      }
#endif
      fp = fopen(argv[1], "r");
      if (fp != NULL)
      {
         yyin = fp;
         //yylex();  // called by yacc
         yyparse();

         // check if all tags were closed, i.e. if the stack is empty
         if (xps.stackDepth > 0)
         {
            debug1("XmltvTag-StartScan: at end-of-file: tag not closed: '%s'", XML_STR_BUF_GET_STR(xps.tagStack[xps.stackDepth - 1].tagName));
            xps.syntaxError += 1;
         }

         fclose(fp);
      }
      else
         fprintf(stderr, "Failed to open file %s: %s\n", argv[1], strerror(errno));
   }
   else
      fprintf(stderr, "Usage: %s <xml-file>\n", argv[0]);

   XmlScan_Destroy();
   XmltvTags_Destroy();

#if CHK_MALLOC == ON
   chk_memleakage();
#endif

   return ((xps.syntaxError) ? -1 : 0);
}

