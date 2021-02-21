/* A Bison parser, made by GNU Bison 3.5.1.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2020 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

#ifndef YY_YY_XMLTV_XML_PROLOG_TAB_H_INCLUDED
# define YY_YY_XMLTV_XML_PROLOG_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    XMLTOK_NAME = 258,
    XMLTOK_ENCNAME = 259,
    XMLTOK_ENCVERS = 260,
    XMLTOK_ATTVAL = 261,
    XMLTOK_NMTOKEN = 262,
    XMLTOK_TAGDOC = 263,
    XMLTOK_TAGENC = 264,
    XMLTOK_TAGENT = 265,
    XMLTOK_TAGPI = 266,
    XMLTOK_TAGELEM = 267,
    XMLTOK_TAGATT = 268,
    XMLTOK_TAGNOT = 269,
    XMLTOK_SYSLIT = 270,
    XMLTOK_PUBLIT = 271,
    XMLTOK_ENTVAL = 272,
    XMLTOK_PICONTENT = 273,
    XMLTOK_CLOSE = 274,
    XMLTOK_SPACE = 275,
    XMLTOK_CONTENT = 276,
    XMLTOK_CONTENT_END = 277,
    XMLTOK_ENCODING = 278,
    XMLTOK_STANDALONE = 279,
    XMLTOK_YES = 280,
    XMLTOK_NO = 281,
    XMLTOK_CHR_PERCENT = 282,
    XMLTOK_VERSION = 283,
    XMLTOK_NDATA = 284,
    XMLTOK_EQ = 285,
    XMLTOK_STR_PUBLIC = 286,
    XMLTOK_STR_SYSTEM = 287,
    XMLTOK_DTD_OPEN = 288,
    XMLTOK_DTD_CLOSE = 289,
    XMLTOK_STR_EMPTY = 290,
    XMLTOK_STR_ANY = 291,
    XMLTOK_STR_PCDATA = 292,
    XMLTOK_CHR_OPBR = 293,
    XMLTOK_CHR_CLBR = 294,
    XMLTOK_CHR_ASTX = 295,
    XMLTOK_CHR_PLUS = 296,
    XMLTOK_CHR_QUMK = 297,
    XMLTOK_CHR_PIPE = 298,
    XMLTOK_CHR_COMA = 299,
    XMLTOK_STR_FIXED = 300,
    XMLTOK_STR_NOTATION = 301,
    XMLTOK_STR_ENTITY = 302,
    XMLTOK_STR_ENTITIES = 303,
    XMLTOK_STR_REQUIRED = 304,
    XMLTOK_STR_NMTOKEN = 305,
    XMLTOK_STR_CDATA = 306,
    XMLTOK_STR_IMPLIED = 307,
    XMLTOK_STR_NMTOKENS = 308,
    XMLTOK_STR_ID = 309,
    XMLTOK_STR_IDREF = 310,
    XMLTOK_STR_IDREFS = 311,
    XMLTOK_ERR_CLOSE = 312
  };
#endif
/* Tokens.  */
#define XMLTOK_NAME 258
#define XMLTOK_ENCNAME 259
#define XMLTOK_ENCVERS 260
#define XMLTOK_ATTVAL 261
#define XMLTOK_NMTOKEN 262
#define XMLTOK_TAGDOC 263
#define XMLTOK_TAGENC 264
#define XMLTOK_TAGENT 265
#define XMLTOK_TAGPI 266
#define XMLTOK_TAGELEM 267
#define XMLTOK_TAGATT 268
#define XMLTOK_TAGNOT 269
#define XMLTOK_SYSLIT 270
#define XMLTOK_PUBLIT 271
#define XMLTOK_ENTVAL 272
#define XMLTOK_PICONTENT 273
#define XMLTOK_CLOSE 274
#define XMLTOK_SPACE 275
#define XMLTOK_CONTENT 276
#define XMLTOK_CONTENT_END 277
#define XMLTOK_ENCODING 278
#define XMLTOK_STANDALONE 279
#define XMLTOK_YES 280
#define XMLTOK_NO 281
#define XMLTOK_CHR_PERCENT 282
#define XMLTOK_VERSION 283
#define XMLTOK_NDATA 284
#define XMLTOK_EQ 285
#define XMLTOK_STR_PUBLIC 286
#define XMLTOK_STR_SYSTEM 287
#define XMLTOK_DTD_OPEN 288
#define XMLTOK_DTD_CLOSE 289
#define XMLTOK_STR_EMPTY 290
#define XMLTOK_STR_ANY 291
#define XMLTOK_STR_PCDATA 292
#define XMLTOK_CHR_OPBR 293
#define XMLTOK_CHR_CLBR 294
#define XMLTOK_CHR_ASTX 295
#define XMLTOK_CHR_PLUS 296
#define XMLTOK_CHR_QUMK 297
#define XMLTOK_CHR_PIPE 298
#define XMLTOK_CHR_COMA 299
#define XMLTOK_STR_FIXED 300
#define XMLTOK_STR_NOTATION 301
#define XMLTOK_STR_ENTITY 302
#define XMLTOK_STR_ENTITIES 303
#define XMLTOK_STR_REQUIRED 304
#define XMLTOK_STR_NMTOKEN 305
#define XMLTOK_STR_CDATA 306
#define XMLTOK_STR_IMPLIED 307
#define XMLTOK_STR_NMTOKENS 308
#define XMLTOK_STR_ID 309
#define XMLTOK_STR_IDREF 310
#define XMLTOK_STR_IDREFS 311
#define XMLTOK_ERR_CLOSE 312

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 48 "xmltv/xml_prolog.yy"
 char * str; 

#line 174 "xmltv/xml_prolog.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_XMLTV_XML_PROLOG_TAB_H_INCLUDED  */
