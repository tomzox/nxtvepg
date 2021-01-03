/* A Bison parser, made by GNU Bison 3.5.1.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Undocumented macros, especially those whose name start with YY_,
   are private implementation details.  Do not rely on them.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.5.1"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 31 "xmltv/xml_prolog.yy"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epgctl/mytypes.h"
#include "epgctl/debug.h"

#define YYDEBUG DEBUG_SWITCH_XMLTV

#include "xmltv/xml_prolog.tab.h"
#include "xmltv/xml_cdata.h"
#include "xmltv/xmltv_db.h"
#include "xmltv/xmltv_tags.h"

extern int yylex( void );
void yyerror( const char * p_msg );

#line 89 "xmltv/xml_prolog.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Use api.header.include to #include this header
   instead of duplicating it here.  */
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
#line 50 "xmltv/xml_prolog.yy"
 char * str; 

#line 258 "xmltv/xml_prolog.tab.c"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_XMLTV_XML_PROLOG_TAB_H_INCLUDED  */



#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))

/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && ! defined __ICC && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                            \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  12
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   302

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  58
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  85
/* YYNRULES -- Number of rules.  */
#define YYNRULES  148
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  276

#define YYUNDEFTOK  2
#define YYMAXUTOK   312


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    72,    72,    73,    77,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    86,    87,    88,    91,    91,
      92,    92,    92,    93,    94,    95,    98,    98,    99,   100,
     101,   102,   103,   104,   105,   105,   106,   106,   107,   108,
     109,   110,   111,   112,   114,   122,   123,   124,   124,   125,
     125,   128,   128,   129,   130,   131,   131,   132,   132,   133,
     134,   135,   135,   136,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   150,   151,   152,   153,
     153,   154,   155,   156,   159,   159,   160,   161,   161,   162,
     162,   162,   165,   166,   166,   167,   168,   168,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   177,   178,
     179,   179,   180,   181,   181,   182,   183,   184,   185,   185,
     188,   189,   190,   190,   190,   191,   195,   196,   197,   197,
     198,   198,   201,   202,   202,   203,   204,   205,   206,   207,
     208,   210,   211,   212,   213,   215,   216,   217,   218
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "XMLTOK_NAME", "XMLTOK_ENCNAME",
  "XMLTOK_ENCVERS", "XMLTOK_ATTVAL", "XMLTOK_NMTOKEN", "XMLTOK_TAGDOC",
  "XMLTOK_TAGENC", "XMLTOK_TAGENT", "XMLTOK_TAGPI", "XMLTOK_TAGELEM",
  "XMLTOK_TAGATT", "XMLTOK_TAGNOT", "XMLTOK_SYSLIT", "XMLTOK_PUBLIT",
  "XMLTOK_ENTVAL", "XMLTOK_PICONTENT", "XMLTOK_CLOSE", "XMLTOK_SPACE",
  "XMLTOK_CONTENT", "XMLTOK_CONTENT_END", "XMLTOK_ENCODING",
  "XMLTOK_STANDALONE", "XMLTOK_YES", "XMLTOK_NO", "XMLTOK_CHR_PERCENT",
  "XMLTOK_VERSION", "XMLTOK_NDATA", "XMLTOK_EQ", "XMLTOK_STR_PUBLIC",
  "XMLTOK_STR_SYSTEM", "XMLTOK_DTD_OPEN", "XMLTOK_DTD_CLOSE",
  "XMLTOK_STR_EMPTY", "XMLTOK_STR_ANY", "XMLTOK_STR_PCDATA",
  "XMLTOK_CHR_OPBR", "XMLTOK_CHR_CLBR", "XMLTOK_CHR_ASTX",
  "XMLTOK_CHR_PLUS", "XMLTOK_CHR_QUMK", "XMLTOK_CHR_PIPE",
  "XMLTOK_CHR_COMA", "XMLTOK_STR_FIXED", "XMLTOK_STR_NOTATION",
  "XMLTOK_STR_ENTITY", "XMLTOK_STR_ENTITIES", "XMLTOK_STR_REQUIRED",
  "XMLTOK_STR_NMTOKEN", "XMLTOK_STR_CDATA", "XMLTOK_STR_IMPLIED",
  "XMLTOK_STR_NMTOKENS", "XMLTOK_STR_ID", "XMLTOK_STR_IDREF",
  "XMLTOK_STR_IDREFS", "XMLTOK_ERR_CLOSE", "$accept", "xml", "prolog",
  "XMLDeclO", "XMLDecl", "XMLDecl2", "EncodingDecl", "SDDecl", "YesOrNo",
  "VersionInfo", "Eq", "MiscL", "Misc", "PI", "PITarget", "doctypeMiscO",
  "doctypedecl", "docExtIDIntO", "intSubsetDecl", "intSubsetL",
  "intSubset", "markupdecl", "DeclSep", "markupdeclErr", "markupdeclErrE",
  "errorL", "EntityDecl", "GEDecl", "PEDecl", "EntityDef", "PEDef",
  "ExternalID", "NDataDeclO", "NDataDecl", "elementdecl", "contentspec",
  "children", "cp", "choice", "choiceNextL1", "choiceNext", "seq",
  "seqNextL", "seqNext", "Mixed", "MixedPipeL1", "MixedPipe",
  "childReCharO", "childReChar", "AttlistDecl", "AttDefL", "AttDef",
  "AttType", "StringType", "TokenizedType", "EnumeratedType",
  "NotationType", "NotationNameL", "Enumeration", "EnumNmtokenL",
  "DefaultDecl", "DefaultFixedO", "NotationDecl", "ExtOrPubID", "$@1",
  "$@2", "content", "contentL", "contents", "SS", "SO", "EncName",
  "VersionStr", "DocName", "GEName", "PEName", "EntityValue",
  "NotationName", "PubidLiteral", "PIName", "PIContent", "Name", "Nmtoken",
  "SystemLiteral", "AttValue", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_int16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312
};
# endif

#define YYPACT_NINF (-224)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-135)

#define yytable_value_is_error(Yyn) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      13,     1,    53,    20,  -224,  -224,  -224,  -224,  -224,    52,
    -224,    33,  -224,  -224,  -224,  -224,    80,     9,    44,  -224,
     102,    51,    64,    28,     3,    76,    95,  -224,  -224,  -224,
    -224,  -224,  -224,  -224,  -224,  -224,  -224,    64,    64,    52,
      64,  -224,  -224,   114,    77,  -224,  -224,  -224,  -224,   120,
    -224,    76,    94,   123,   103,   112,   121,   126,  -224,  -224,
      64,     7,  -224,    45,   128,   130,  -224,  -224,  -224,  -224,
    -224,    64,  -224,  -224,  -224,   134,   101,   122,  -224,  -224,
    -224,   135,  -224,    76,    76,    49,  -224,  -224,  -224,   141,
     146,   122,    23,  -224,    76,  -224,  -224,  -224,    76,  -224,
      76,    76,    76,    64,  -224,  -224,  -224,  -224,  -224,  -224,
    -224,  -224,  -224,  -224,  -224,   146,    17,   160,   160,   161,
    -224,  -224,  -224,    76,    76,  -224,    76,  -224,  -224,    76,
     162,    -6,    82,    52,   107,  -224,    76,  -224,   148,    52,
      64,  -224,  -224,    64,    64,  -224,    71,    71,  -224,  -224,
     160,   149,    76,    76,   150,    -6,  -224,  -224,    64,   145,
    -224,  -224,    37,   151,  -224,  -224,  -224,  -224,  -224,  -224,
      76,  -224,   159,   163,  -224,    64,  -224,  -224,  -224,    76,
      64,    64,    64,    71,    71,    71,  -224,   222,    52,    64,
     157,   160,    47,    14,    29,   140,  -224,  -224,  -224,    64,
      76,  -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,    76,
    -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,
    -224,    64,    64,  -224,  -224,    64,    64,    64,    64,  -224,
     170,   147,   -33,   171,   160,    50,    14,  -224,    14,    60,
    -224,    64,  -224,    64,    76,  -224,  -224,  -224,   182,  -224,
    -224,   152,  -224,  -224,  -224,  -224,  -224,    65,   160,  -224,
    -224,  -224,    64,  -224,  -224,    64,    64,  -224,  -224,   170,
      67,  -224,  -224,    64,   160,  -224
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       4,     0,     0,     0,    18,     5,    49,   132,    46,   133,
       7,     0,     1,   128,    18,     2,     0,     0,   132,     6,
       0,     0,   133,     0,     0,     0,     0,    19,    20,     3,
      18,    22,    21,    50,    47,    48,    45,   133,   133,   133,
     133,     8,   134,     0,     0,   130,    18,   131,   129,     0,
     143,    24,     0,     0,     0,     0,     0,     0,   136,    16,
     133,     0,   137,    29,     0,     0,   135,    12,    14,    15,
      13,   133,     9,    11,    17,     0,     0,     0,    23,   144,
      25,     0,    28,     0,     0,    30,    34,    32,    10,     0,
       0,     0,     0,   142,     0,   147,    59,    31,     0,    42,
       0,     0,     0,   133,    35,    36,    37,    43,    40,    51,
      52,    38,    39,    41,    44,     0,     0,     0,     0,     0,
      33,    60,   138,     0,     0,   145,     0,    93,   141,     0,
       0,     0,     0,   133,     0,   139,     0,   140,     0,   133,
     133,    65,    66,   133,   133,    68,    87,    87,    67,    94,
       0,     0,     0,     0,     0,     0,    53,    56,   133,     0,
      61,    55,     0,     0,    90,    91,    89,    69,    88,    70,
       0,    92,     0,     0,   120,   133,    58,    57,    62,     0,
     133,   133,   133,    87,    87,    87,    64,     0,   133,   133,
       0,     0,     0,     0,     0,    79,    72,    73,    71,   133,
       0,   103,   104,   105,    99,   106,   100,   101,   102,     0,
      96,    97,    98,   107,   108,   122,   125,   121,    54,    63,
      83,   133,   133,    84,    78,   133,   133,   133,   133,    75,
       0,     0,   118,     0,     0,     0,     0,    80,     0,     0,
     146,   133,   113,   133,     0,   115,   116,    95,     0,   123,
      86,     0,    85,    81,    77,    74,    76,     0,     0,   119,
     148,   117,   133,    82,   112,   133,   133,   110,   124,     0,
       0,   114,   109,   133,     0,   111
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -224,  -224,  -224,  -224,  -224,  -224,  -224,   138,  -224,  -224,
     106,    41,   166,  -224,  -224,  -224,  -224,  -224,   104,  -224,
    -224,  -224,  -224,     0,  -224,  -224,  -224,  -224,  -224,  -224,
    -224,   -99,  -224,  -224,  -224,  -224,  -224,  -223,    66,  -224,
     -43,    68,  -224,  -224,  -224,  -224,   -36,  -138,  -224,  -224,
    -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,
    -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,  -224,    58,
      -9,  -224,  -224,  -224,  -224,  -224,    46,  -224,  -224,  -224,
    -224,  -112,   -67,    88,  -224
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     2,     3,     4,     5,    19,    39,    40,    70,     9,
      43,    16,    27,    28,    64,    29,    30,    75,    87,    92,
     104,   105,   106,    31,    36,    17,   108,   109,   110,   138,
     175,    85,   157,   158,   111,   144,   145,   182,   183,   228,
     229,   184,   194,   226,   148,   222,   223,   167,   168,   112,
     133,   149,   209,   210,   211,   212,   213,   266,   214,   241,
     247,   248,   113,   154,   233,   262,    15,    23,    48,    32,
      44,    67,    59,    63,   124,   136,   140,   129,    94,    51,
      80,   185,   242,    96,   261
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      21,    10,     6,  -127,     6,   126,   127,  -126,     6,   169,
      33,   137,   244,   253,    26,   254,   245,   125,    26,   246,
     122,     7,     1,     7,     6,    83,    84,     7,    34,     6,
      56,    57,   139,    98,    99,   100,   101,   102,   170,    26,
     125,    13,    14,     7,   123,   196,   197,   198,     7,    45,
      46,    74,   181,    12,    77,    24,   176,   103,     8,    11,
       8,    22,    81,  -134,     8,    18,    35,    20,   224,    42,
      41,    52,    18,   225,   180,   181,    91,  -134,  -133,   219,
       8,     6,  -133,    49,    42,     8,   220,    61,    25,   251,
     221,    26,   107,   221,   120,     6,     7,    55,    50,   255,
       7,   -26,   -26,   227,   264,    26,   272,    60,   265,    65,
     273,   164,   165,   166,     7,   -27,   -27,   141,   142,    58,
     143,    76,   250,    62,   151,    37,    38,    66,    68,    69,
     160,   161,    83,    84,   162,   163,    38,     8,   152,   153,
      72,    89,    90,    53,    54,    73,   267,    78,    79,   178,
     114,     8,   115,    82,    88,    86,   116,    93,   117,   118,
     119,    95,   275,   125,   128,   135,   190,   156,   171,   174,
     186,   192,   193,   195,   179,   188,   218,   240,   189,   216,
     217,   130,   131,   227,   132,   243,   249,   134,   260,    47,
     230,   150,   263,    71,   155,    97,   256,   159,   146,   252,
     147,   177,   271,   121,     0,     0,     0,     0,     0,     0,
     172,   173,   234,   235,     0,     0,   236,   237,   238,   239,
       0,     0,     0,     0,     0,     0,     0,     0,   187,     0,
       0,     0,   257,     0,   258,     0,     0,   191,     0,     0,
       0,     0,     0,     0,     0,     0,   215,     0,     0,     0,
       0,     0,     0,   268,     0,     0,   269,   270,   231,     0,
     199,     0,     0,     0,   274,     0,     0,   232,   200,   201,
     202,     0,   203,   204,     0,   205,   206,   207,   208,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   259
};

static const yytype_int16 yycheck[] =
{
       9,     1,     1,     0,     1,   117,   118,     0,     1,   147,
       1,    17,    45,   236,    11,   238,    49,     3,    11,    52,
       3,    20,     9,    20,     1,    31,    32,    20,    19,     1,
      39,    40,   131,    10,    11,    12,    13,    14,   150,    11,
       3,    21,    22,    20,    27,   183,   184,   185,    20,    21,
      22,    60,    38,     0,    63,    14,   155,    34,    57,     1,
      57,    28,    71,    19,    57,    20,    57,     9,    39,    20,
      19,    30,    20,    44,    37,    38,    85,    33,    33,   191,
      57,     1,    33,    25,    20,    57,    39,    46,     8,    39,
      43,    11,    92,    43,   103,     1,    20,    39,     3,    39,
      20,    21,    22,    43,    39,    11,    39,    30,    43,    51,
      43,    40,    41,    42,    20,    21,    22,    35,    36,     5,
      38,    63,   234,     3,   133,    23,    24,     4,    25,    26,
     139,   140,    31,    32,   143,   144,    24,    57,    31,    32,
      19,    83,    84,    37,    38,    19,   258,    19,    18,   158,
      92,    57,    94,    19,    19,    33,    98,    16,   100,   101,
     102,    15,   274,     3,     3,     3,   175,    19,    19,    19,
      19,   180,   181,   182,    29,    16,    19,     7,    15,   188,
     189,   123,   124,    43,   126,    38,    15,   129,     6,    23,
     199,   133,    40,    55,   136,    91,   239,   139,   132,   235,
     132,   155,   269,   115,    -1,    -1,    -1,    -1,    -1,    -1,
     152,   153,   221,   222,    -1,    -1,   225,   226,   227,   228,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   170,    -1,
      -1,    -1,   241,    -1,   243,    -1,    -1,   179,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   188,    -1,    -1,    -1,
      -1,    -1,    -1,   262,    -1,    -1,   265,   266,   200,    -1,
      38,    -1,    -1,    -1,   273,    -1,    -1,   209,    46,    47,
      48,    -1,    50,    51,    -1,    53,    54,    55,    56,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   244
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     9,    59,    60,    61,    62,     1,    20,    57,    67,
      81,   127,     0,    21,    22,   124,    69,    83,    20,    63,
     127,   128,    28,   125,    69,     8,    11,    70,    71,    73,
      74,    81,   127,     1,    19,    57,    82,    23,    24,    64,
      65,    19,    20,    68,   128,    21,    22,    70,   126,   127,
       3,   137,    69,    68,    68,   127,   128,   128,     5,   130,
      30,    69,     3,   131,    72,   127,     4,   129,    25,    26,
      66,    65,    19,    19,   128,    75,   127,   128,    19,    18,
     138,   128,    19,    31,    32,    89,    33,    76,    19,   127,
     127,   128,    77,    16,   136,    15,   141,    76,    10,    11,
      12,    13,    14,    34,    78,    79,    80,    81,    84,    85,
      86,    92,   107,   120,   127,   127,   127,   127,   127,   127,
     128,   141,     3,    27,   132,     3,   139,   139,     3,   135,
     127,   127,   127,   108,   127,     3,   133,    17,    87,    89,
     134,    35,    36,    38,    93,    94,    96,    99,   102,   109,
     127,   128,    31,    32,   121,   127,    19,    90,    91,   127,
     128,   128,   128,   128,    40,    41,    42,   105,   106,   105,
     139,    19,   127,   127,    19,    88,    89,   134,   128,    29,
      37,    38,    95,    96,    99,   139,    19,   127,    16,    15,
     128,   127,   128,   128,   100,   128,   105,   105,   105,    38,
      46,    47,    48,    50,    51,    53,    54,    55,    56,   110,
     111,   112,   113,   114,   116,   127,   128,   128,    19,   139,
      39,    43,   103,   104,    39,    44,   101,    43,    97,    98,
     128,   127,   127,   122,   128,   128,   128,   128,   128,   128,
       7,   117,   140,    38,    45,    49,    52,   118,   119,    15,
     139,    39,   104,    95,    95,    39,    98,   128,   128,   127,
       6,   142,   123,    40,    39,    43,   115,   139,   128,   128,
     128,   140,    39,    43,   128,   139
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    58,    59,    60,    61,    61,    62,    62,    63,    63,
      63,    63,    64,    65,    66,    66,    67,    68,    69,    69,
      70,    70,    70,    71,    72,    72,    73,    73,    74,    75,
      75,    75,    75,    76,    77,    77,    78,    78,    79,    79,
      79,    79,    79,    79,    80,    81,    81,    82,    82,    83,
      83,    84,    84,    85,    86,    87,    87,    88,    88,    89,
      89,    90,    90,    91,    92,    93,    93,    93,    93,    94,
      94,    95,    95,    95,    96,    97,    97,    98,    99,   100,
     100,   101,   102,   102,   103,   103,   104,   105,   105,   106,
     106,   106,   107,   108,   108,   109,   110,   110,   110,   111,
     112,   112,   112,   112,   112,   112,   112,   113,   113,   114,
     115,   115,   116,   117,   117,   118,   118,   118,   119,   119,
     120,   121,   122,   123,   121,   121,   124,   124,   125,   125,
     126,   126,   127,   128,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     3,     0,     1,     3,     2,     2,     4,
       6,     4,     3,     3,     1,     1,     4,     3,     0,     2,
       1,     1,     1,     4,     0,     2,     0,     2,     5,     0,
       2,     4,     2,     4,     0,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     1,     1,     1,     0,
       2,     1,     1,     6,     9,     2,     2,     1,     1,     3,
       5,     1,     2,     4,     7,     1,     1,     1,     1,     2,
       2,     2,     2,     2,     7,     1,     3,     3,     5,     1,
       3,     3,     8,     5,     1,     3,     3,     0,     1,     1,
       1,     1,     6,     0,     2,     6,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     7,
       1,     5,     5,     1,     5,     1,     1,     2,     0,     2,
       6,     4,     0,     0,     8,     4,     4,     2,     0,     2,
       1,     1,     1,     0,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YYUSE (yyoutput);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyo, yytoknum[yytype], *yyvaluep);
# endif
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo, int yytype, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyo, yytype, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[+yyssp[yyi + 1 - yynrhs]],
                       &yyvsp[(yyi + 1) - (yynrhs)]
                                              );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen(S) (YY_CAST (YYPTRDIFF_T, strlen (S)))
#  else
/* Return the length of YYSTR.  */
static YYPTRDIFF_T
yystrlen (const char *yystr)
{
  YYPTRDIFF_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYPTRDIFF_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYPTRDIFF_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
        switch (*++yyp)
          {
          case '\'':
          case ',':
            goto do_not_strip_quotes;

          case '\\':
            if (*++yyp != '\\')
              goto do_not_strip_quotes;
            else
              goto append;

          append:
          default:
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (yyres)
    return yystpcpy (yyres, yystr) - yyres;
  else
    return yystrlen (yystr);
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYPTRDIFF_T *yymsg_alloc, char **yymsg,
                yy_state_t *yyssp, int yytoken)
{
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat: reported tokens (one for the "unexpected",
     one per "expected"). */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Actual size of YYARG. */
  int yycount = 0;
  /* Cumulated lengths of YYARG.  */
  YYPTRDIFF_T yysize = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[+*yyssp];
      YYPTRDIFF_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
      yysize = yysize0;
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYPTRDIFF_T yysize1
                    = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
                    yysize = yysize1;
                  else
                    return 2;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
    default: /* Avoid compiler warnings. */
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    /* Don't count the "%s"s in the final size, but reserve room for
       the terminator.  */
    YYPTRDIFF_T yysize1 = yysize + (yystrlen (yyformat) - 2 * yycount) + 1;
    if (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM)
      yysize = yysize1;
    else
      return 2;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          ++yyp;
          ++yyformat;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
{
  YYUSE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;


/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss;
    yy_state_t *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYPTRDIFF_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYPTRDIFF_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    goto yyexhaustedlab;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
# undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 24:
#line 94 "xmltv/xml_prolog.yy"
                  { XmltvTags_PiContent(NULL); }
#line 1661 "xmltv/xml_prolog.tab.c"
    break;

  case 31:
#line 102 "xmltv/xml_prolog.yy"
                                                 {XmltvTags_DocIntDtdClose();}
#line 1667 "xmltv/xml_prolog.tab.c"
    break;

  case 32:
#line 103 "xmltv/xml_prolog.yy"
                                   {XmltvTags_DocIntDtdClose();}
#line 1673 "xmltv/xml_prolog.tab.c"
    break;

  case 45:
#line 122 "xmltv/xml_prolog.yy"
                                              {yyerrok;}
#line 1679 "xmltv/xml_prolog.tab.c"
    break;

  case 46:
#line 123 "xmltv/xml_prolog.yy"
                                   {yyerrok;}
#line 1685 "xmltv/xml_prolog.tab.c"
    break;

  case 121:
#line 189 "xmltv/xml_prolog.yy"
                                                        {XmltvTags_Notation(1, (yyvsp[-1].str));}
#line 1691 "xmltv/xml_prolog.tab.c"
    break;

  case 122:
#line 190 "xmltv/xml_prolog.yy"
                                                        {XmltvTags_Notation(2, (yyvsp[-1].str));}
#line 1697 "xmltv/xml_prolog.tab.c"
    break;

  case 123:
#line 190 "xmltv/xml_prolog.yy"
                                                                                                   {XmltvTags_Notation(4, (yyvsp[0].str));}
#line 1703 "xmltv/xml_prolog.tab.c"
    break;

  case 125:
#line 191 "xmltv/xml_prolog.yy"
                                                        {XmltvTags_Notation(3, (yyvsp[-1].str));}
#line 1709 "xmltv/xml_prolog.tab.c"
    break;

  case 135:
#line 203 "xmltv/xml_prolog.yy"
                                        { XmltvTags_Encoding((yyvsp[0].str)); }
#line 1715 "xmltv/xml_prolog.tab.c"
    break;

  case 136:
#line 204 "xmltv/xml_prolog.yy"
                                        { XmltvTags_XmlVersion((yyvsp[0].str)); }
#line 1721 "xmltv/xml_prolog.tab.c"
    break;

  case 137:
#line 205 "xmltv/xml_prolog.yy"
                                        { XmltvTags_DocType((yyvsp[0].str)); }
#line 1727 "xmltv/xml_prolog.tab.c"
    break;

  case 138:
#line 206 "xmltv/xml_prolog.yy"
                                        { XmlScan_EntityDefName((yyvsp[0].str), 0); XmltvTags_CheckName((yyvsp[0].str)); }
#line 1733 "xmltv/xml_prolog.tab.c"
    break;

  case 139:
#line 207 "xmltv/xml_prolog.yy"
                                        { XmlScan_EntityDefName((yyvsp[0].str), 1); XmltvTags_CheckName((yyvsp[0].str)); }
#line 1739 "xmltv/xml_prolog.tab.c"
    break;

  case 140:
#line 208 "xmltv/xml_prolog.yy"
                                        { XmlScan_EntityDefValue((yyvsp[0].str)); XmltvTags_CheckCharset((yyvsp[0].str)); }
#line 1745 "xmltv/xml_prolog.tab.c"
    break;

  case 141:
#line 210 "xmltv/xml_prolog.yy"
                                        { XmltvTags_Notation(0, (yyvsp[0].str)); }
#line 1751 "xmltv/xml_prolog.tab.c"
    break;

  case 142:
#line 211 "xmltv/xml_prolog.yy"
                                        { }
#line 1757 "xmltv/xml_prolog.tab.c"
    break;

  case 143:
#line 212 "xmltv/xml_prolog.yy"
                                        { XmltvTags_PiTarget((yyvsp[0].str)); }
#line 1763 "xmltv/xml_prolog.tab.c"
    break;

  case 144:
#line 213 "xmltv/xml_prolog.yy"
                                        { XmltvTags_PiContent((yyvsp[0].str)); }
#line 1769 "xmltv/xml_prolog.tab.c"
    break;

  case 145:
#line 215 "xmltv/xml_prolog.yy"
                                        { XmltvTags_CheckName((yyvsp[0].str)); }
#line 1775 "xmltv/xml_prolog.tab.c"
    break;

  case 146:
#line 216 "xmltv/xml_prolog.yy"
                                        { XmltvTags_CheckNmtoken((yyvsp[0].str)); }
#line 1781 "xmltv/xml_prolog.tab.c"
    break;

  case 147:
#line 217 "xmltv/xml_prolog.yy"
                                        { XmltvTags_CheckSystemLiteral((yyvsp[0].str)); }
#line 1787 "xmltv/xml_prolog.tab.c"
    break;

  case 148:
#line 218 "xmltv/xml_prolog.yy"
                                        { XmltvTags_CheckCharset((yyvsp[0].str)); }
#line 1793 "xmltv/xml_prolog.tab.c"
    break;


#line 1797 "xmltv/xml_prolog.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = YY_CAST (char *, YYSTACK_ALLOC (YY_CAST (YYSIZE_T, yymsg_alloc)));
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYTERROR;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;


#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif


/*-----------------------------------------------------.
| yyreturn -- parsing is finished, return the result.  |
`-----------------------------------------------------*/
yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[+*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 220 "xmltv/xml_prolog.yy"


void yyerror( const char * p_msg )
{
   Xmltv_SyntaxError("Prolog parse error", p_msg);
}

