/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

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

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1


/* Substitute the variable and function names.  */
#define yyparse         _mesa_glsl_parse
#define yylex           _mesa_glsl_lex
#define yyerror         _mesa_glsl_error
#define yydebug         _mesa_glsl_debug
#define yynerrs         _mesa_glsl_nerrs


/* Copy the first part of user declarations.  */
#line 1 "./glsl/glsl_parser.yy" /* yacc.c:339  */

/*
 * Copyright © 2008, 2009 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include <assert.h>

#include "ast.h"
#include "glsl_parser_extras.h"
#include "compiler/glsl_types.h"
#include "main/context.h"

#ifdef _MSC_VER
#pragma warning( disable : 4065 ) // switch statement contains 'default' but no 'case' labels
#endif

#undef yyerror

static void yyerror(YYLTYPE *loc, _mesa_glsl_parse_state *st, const char *msg)
{
   _mesa_glsl_error(loc, st, "%s", msg);
}

static int
_mesa_glsl_lex(YYSTYPE *val, YYLTYPE *loc, _mesa_glsl_parse_state *state)
{
   return _mesa_glsl_lexer_lex(val, loc, state->scanner);
}

static bool match_layout_qualifier(const char *s1, const char *s2,
                                   _mesa_glsl_parse_state *state)
{
   /* From the GLSL 1.50 spec, section 4.3.8 (Layout Qualifiers):
    *
    *     "The tokens in any layout-qualifier-id-list ... are not case
    *     sensitive, unless explicitly noted otherwise."
    *
    * The text "unless explicitly noted otherwise" appears to be
    * vacuous--no desktop GLSL spec (up through GLSL 4.40) notes
    * otherwise.
    *
    * However, the GLSL ES 3.00 spec says, in section 4.3.8 (Layout
    * Qualifiers):
    *
    *     "As for other identifiers, they are case sensitive."
    *
    * So we need to do a case-sensitive or a case-insensitive match,
    * depending on whether we are compiling for GLSL ES.
    */
   if (state->es_shader)
      return strcmp(s1, s2);
   else
      return strcasecmp(s1, s2);
}

#line 152 "glsl/glsl_parser.cpp" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

/* In a future release of Bison, this section will be replaced
   by #include "glsl_parser.h".  */
#ifndef YY__MESA_GLSL_GLSL_GLSL_PARSER_H_INCLUDED
# define YY__MESA_GLSL_GLSL_GLSL_PARSER_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int _mesa_glsl_debug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    ATTRIBUTE = 258,
    CONST_TOK = 259,
    BASIC_TYPE_TOK = 260,
    BREAK = 261,
    BUFFER = 262,
    CONTINUE = 263,
    DO = 264,
    ELSE = 265,
    FOR = 266,
    IF = 267,
    DISCARD = 268,
    RETURN = 269,
    SWITCH = 270,
    CASE = 271,
    DEFAULT = 272,
    CENTROID = 273,
    IN_TOK = 274,
    OUT_TOK = 275,
    INOUT_TOK = 276,
    UNIFORM = 277,
    VARYING = 278,
    SAMPLE = 279,
    NOPERSPECTIVE = 280,
    FLAT = 281,
    SMOOTH = 282,
    IMAGE1DSHADOW = 283,
    IMAGE2DSHADOW = 284,
    IMAGE1DARRAYSHADOW = 285,
    IMAGE2DARRAYSHADOW = 286,
    COHERENT = 287,
    VOLATILE = 288,
    RESTRICT = 289,
    READONLY = 290,
    WRITEONLY = 291,
    SHARED = 292,
    STRUCT = 293,
    VOID_TOK = 294,
    WHILE = 295,
    IDENTIFIER = 296,
    TYPE_IDENTIFIER = 297,
    NEW_IDENTIFIER = 298,
    FLOATCONSTANT = 299,
    DOUBLECONSTANT = 300,
    INTCONSTANT = 301,
    UINTCONSTANT = 302,
    BOOLCONSTANT = 303,
    INT64CONSTANT = 304,
    UINT64CONSTANT = 305,
    FIELD_SELECTION = 306,
    LEFT_OP = 307,
    RIGHT_OP = 308,
    INC_OP = 309,
    DEC_OP = 310,
    LE_OP = 311,
    GE_OP = 312,
    EQ_OP = 313,
    NE_OP = 314,
    AND_OP = 315,
    OR_OP = 316,
    XOR_OP = 317,
    MUL_ASSIGN = 318,
    DIV_ASSIGN = 319,
    ADD_ASSIGN = 320,
    MOD_ASSIGN = 321,
    LEFT_ASSIGN = 322,
    RIGHT_ASSIGN = 323,
    AND_ASSIGN = 324,
    XOR_ASSIGN = 325,
    OR_ASSIGN = 326,
    SUB_ASSIGN = 327,
    INVARIANT = 328,
    PRECISE = 329,
    LOWP = 330,
    MEDIUMP = 331,
    HIGHP = 332,
    SUPERP = 333,
    PRECISION = 334,
    VERSION_TOK = 335,
    EXTENSION = 336,
    LINE = 337,
    COLON = 338,
    EOL = 339,
    INTERFACE = 340,
    OUTPUT = 341,
    PRAGMA_DEBUG_ON = 342,
    PRAGMA_DEBUG_OFF = 343,
    PRAGMA_OPTIMIZE_ON = 344,
    PRAGMA_OPTIMIZE_OFF = 345,
    PRAGMA_INVARIANT_ALL = 346,
    LAYOUT_TOK = 347,
    DOT_TOK = 348,
    ASM = 349,
    CLASS = 350,
    UNION = 351,
    ENUM = 352,
    TYPEDEF = 353,
    TEMPLATE = 354,
    THIS = 355,
    PACKED_TOK = 356,
    GOTO = 357,
    INLINE_TOK = 358,
    NOINLINE = 359,
    PUBLIC_TOK = 360,
    STATIC = 361,
    EXTERN = 362,
    EXTERNAL = 363,
    LONG_TOK = 364,
    SHORT_TOK = 365,
    HALF = 366,
    FIXED_TOK = 367,
    UNSIGNED = 368,
    INPUT_TOK = 369,
    HVEC2 = 370,
    HVEC3 = 371,
    HVEC4 = 372,
    FVEC2 = 373,
    FVEC3 = 374,
    FVEC4 = 375,
    SAMPLER3DRECT = 376,
    SIZEOF = 377,
    CAST = 378,
    NAMESPACE = 379,
    USING = 380,
    RESOURCE = 381,
    PATCH = 382,
    SUBROUTINE = 383,
    ERROR_TOK = 384,
    COMMON = 385,
    PARTITION = 386,
    ACTIVE = 387,
    FILTER = 388,
    ROW_MAJOR = 389,
    THEN = 390
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 98 "./glsl/glsl_parser.yy" /* yacc.c:355  */

   int n;
   int64_t n64;
   float real;
   double dreal;
   const char *identifier;

   struct ast_type_qualifier type_qualifier;

   ast_node *node;
   ast_type_specifier *type_specifier;
   ast_array_specifier *array_specifier;
   ast_fully_specified_type *fully_specified_type;
   ast_function *function;
   ast_parameter_declarator *parameter_declarator;
   ast_function_definition *function_definition;
   ast_compound_statement *compound_statement;
   ast_expression *expression;
   ast_declarator_list *declarator_list;
   ast_struct_specifier *struct_specifier;
   ast_declaration *declaration;
   ast_switch_body *switch_body;
   ast_case_label *case_label;
   ast_case_label_list *case_label_list;
   ast_case_statement *case_statement;
   ast_case_statement_list *case_statement_list;
   ast_interface_block *interface_block;
   ast_subroutine_list *subroutine_list;
   struct {
      ast_node *cond;
      ast_expression *rest;
   } for_rest_statement;

   struct {
      ast_node *then_statement;
      ast_node *else_statement;
   } selection_rest_statement;

   const glsl_type *type;

#line 369 "glsl/glsl_parser.cpp" /* yacc.c:355  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif

/* Location type.  */
#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE YYLTYPE;
struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
};
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif



int _mesa_glsl_parse (struct _mesa_glsl_parse_state *state);

#endif /* !YY__MESA_GLSL_GLSL_GLSL_PARSER_H_INCLUDED  */

/* Copy the second part of user declarations.  */

#line 399 "glsl/glsl_parser.cpp" /* yacc.c:358  */

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

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

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
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
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE) + sizeof (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  5
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   2209

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  159
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  110
/* YYNRULES -- Number of rules.  */
#define YYNRULES  304
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  461

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   390

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   143,     2,     2,     2,   147,   150,     2,
     136,   137,   145,   141,   140,   142,     2,   146,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   154,   156,
     148,   155,   149,   153,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   138,     2,   139,   151,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   157,   152,   158,   144,     2,     2,     2,
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
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   289,   289,   288,   312,   314,   321,   331,   332,   333,
     334,   335,   359,   361,   365,   366,   367,   371,   380,   388,
     396,   407,   408,   412,   419,   426,   433,   440,   447,   454,
     461,   468,   475,   476,   482,   486,   493,   499,   508,   512,
     516,   520,   521,   525,   526,   530,   536,   548,   552,   558,
     572,   573,   579,   585,   595,   596,   597,   598,   602,   603,
     609,   615,   624,   625,   631,   640,   641,   647,   656,   657,
     663,   669,   675,   684,   685,   691,   700,   701,   710,   711,
     720,   721,   730,   731,   740,   741,   750,   751,   760,   761,
     770,   771,   780,   781,   782,   783,   784,   785,   786,   787,
     788,   789,   790,   794,   798,   814,   818,   823,   827,   832,
     849,   853,   854,   858,   863,   871,   889,   900,   915,   923,
     940,   943,   951,   959,   971,   983,   990,   995,  1000,  1009,
    1013,  1014,  1024,  1034,  1044,  1058,  1065,  1076,  1087,  1098,
    1109,  1121,  1136,  1143,  1161,  1168,  1169,  1179,  1595,  1760,
    1786,  1791,  1796,  1804,  1809,  1818,  1827,  1839,  1844,  1849,
    1858,  1863,  1868,  1869,  1870,  1871,  1872,  1873,  1874,  1892,
    1900,  1925,  1949,  1963,  1968,  1984,  2004,  2016,  2024,  2029,
    2034,  2041,  2046,  2051,  2056,  2061,  2086,  2098,  2103,  2108,
    2116,  2121,  2126,  2132,  2137,  2145,  2153,  2159,  2169,  2180,
    2181,  2189,  2195,  2201,  2210,  2211,  2215,  2220,  2225,  2233,
    2240,  2257,  2262,  2270,  2308,  2313,  2321,  2327,  2336,  2337,
    2341,  2348,  2355,  2362,  2368,  2369,  2373,  2374,  2375,  2376,
    2377,  2378,  2382,  2389,  2388,  2402,  2403,  2407,  2413,  2422,
    2432,  2444,  2450,  2459,  2468,  2473,  2481,  2485,  2503,  2511,
    2516,  2524,  2529,  2537,  2545,  2553,  2561,  2569,  2577,  2585,
    2592,  2599,  2609,  2610,  2614,  2616,  2622,  2627,  2636,  2642,
    2648,  2654,  2660,  2669,  2670,  2671,  2672,  2676,  2690,  2694,
    2707,  2725,  2744,  2749,  2754,  2759,  2764,  2779,  2782,  2787,
    2795,  2800,  2808,  2832,  2839,  2843,  2850,  2854,  2864,  2873,
    2883,  2892,  2904,  2926,  2936
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ATTRIBUTE", "CONST_TOK",
  "BASIC_TYPE_TOK", "BREAK", "BUFFER", "CONTINUE", "DO", "ELSE", "FOR",
  "IF", "DISCARD", "RETURN", "SWITCH", "CASE", "DEFAULT", "CENTROID",
  "IN_TOK", "OUT_TOK", "INOUT_TOK", "UNIFORM", "VARYING", "SAMPLE",
  "NOPERSPECTIVE", "FLAT", "SMOOTH", "IMAGE1DSHADOW", "IMAGE2DSHADOW",
  "IMAGE1DARRAYSHADOW", "IMAGE2DARRAYSHADOW", "COHERENT", "VOLATILE",
  "RESTRICT", "READONLY", "WRITEONLY", "SHARED", "STRUCT", "VOID_TOK",
  "WHILE", "IDENTIFIER", "TYPE_IDENTIFIER", "NEW_IDENTIFIER",
  "FLOATCONSTANT", "DOUBLECONSTANT", "INTCONSTANT", "UINTCONSTANT",
  "BOOLCONSTANT", "INT64CONSTANT", "UINT64CONSTANT", "FIELD_SELECTION",
  "LEFT_OP", "RIGHT_OP", "INC_OP", "DEC_OP", "LE_OP", "GE_OP", "EQ_OP",
  "NE_OP", "AND_OP", "OR_OP", "XOR_OP", "MUL_ASSIGN", "DIV_ASSIGN",
  "ADD_ASSIGN", "MOD_ASSIGN", "LEFT_ASSIGN", "RIGHT_ASSIGN", "AND_ASSIGN",
  "XOR_ASSIGN", "OR_ASSIGN", "SUB_ASSIGN", "INVARIANT", "PRECISE", "LOWP",
  "MEDIUMP", "HIGHP", "SUPERP", "PRECISION", "VERSION_TOK", "EXTENSION",
  "LINE", "COLON", "EOL", "INTERFACE", "OUTPUT", "PRAGMA_DEBUG_ON",
  "PRAGMA_DEBUG_OFF", "PRAGMA_OPTIMIZE_ON", "PRAGMA_OPTIMIZE_OFF",
  "PRAGMA_INVARIANT_ALL", "LAYOUT_TOK", "DOT_TOK", "ASM", "CLASS", "UNION",
  "ENUM", "TYPEDEF", "TEMPLATE", "THIS", "PACKED_TOK", "GOTO",
  "INLINE_TOK", "NOINLINE", "PUBLIC_TOK", "STATIC", "EXTERN", "EXTERNAL",
  "LONG_TOK", "SHORT_TOK", "HALF", "FIXED_TOK", "UNSIGNED", "INPUT_TOK",
  "HVEC2", "HVEC3", "HVEC4", "FVEC2", "FVEC3", "FVEC4", "SAMPLER3DRECT",
  "SIZEOF", "CAST", "NAMESPACE", "USING", "RESOURCE", "PATCH",
  "SUBROUTINE", "ERROR_TOK", "COMMON", "PARTITION", "ACTIVE", "FILTER",
  "ROW_MAJOR", "THEN", "'('", "')'", "'['", "']'", "','", "'+'", "'-'",
  "'!'", "'~'", "'*'", "'/'", "'%'", "'<'", "'>'", "'&'", "'^'", "'|'",
  "'?'", "':'", "'='", "';'", "'{'", "'}'", "$accept", "translation_unit",
  "$@1", "version_statement", "pragma_statement",
  "extension_statement_list", "any_identifier", "extension_statement",
  "external_declaration_list", "variable_identifier", "primary_expression",
  "postfix_expression", "integer_expression", "function_call",
  "function_call_or_method", "function_call_generic",
  "function_call_header_no_parameters",
  "function_call_header_with_parameters", "function_call_header",
  "function_identifier", "unary_expression", "unary_operator",
  "multiplicative_expression", "additive_expression", "shift_expression",
  "relational_expression", "equality_expression", "and_expression",
  "exclusive_or_expression", "inclusive_or_expression",
  "logical_and_expression", "logical_xor_expression",
  "logical_or_expression", "conditional_expression",
  "assignment_expression", "assignment_operator", "expression",
  "constant_expression", "declaration", "function_prototype",
  "function_declarator", "function_header_with_parameters",
  "function_header", "parameter_declarator", "parameter_declaration",
  "parameter_qualifier", "parameter_direction_qualifier",
  "parameter_type_specifier", "init_declarator_list", "single_declaration",
  "fully_specified_type", "layout_qualifier", "layout_qualifier_id_list",
  "layout_qualifier_id", "interface_block_layout_qualifier",
  "subroutine_qualifier", "subroutine_type_list",
  "interpolation_qualifier", "type_qualifier",
  "auxiliary_storage_qualifier", "storage_qualifier", "memory_qualifier",
  "array_specifier", "type_specifier", "type_specifier_nonarray",
  "basic_type_specifier_nonarray", "precision_qualifier",
  "struct_specifier", "struct_declaration_list", "struct_declaration",
  "struct_declarator_list", "struct_declarator", "initializer",
  "initializer_list", "declaration_statement", "statement",
  "simple_statement", "compound_statement", "$@2",
  "statement_no_new_scope", "compound_statement_no_new_scope",
  "statement_list", "expression_statement", "selection_statement",
  "selection_rest_statement", "condition", "switch_statement",
  "switch_body", "case_label", "case_label_list", "case_statement",
  "case_statement_list", "iteration_statement", "for_init_statement",
  "conditionopt", "for_rest_statement", "jump_statement",
  "external_declaration", "function_definition", "interface_block",
  "basic_interface_block", "interface_qualifier", "instance_name_opt",
  "member_list", "member_declaration", "layout_uniform_defaults",
  "layout_buffer_defaults", "layout_in_defaults", "layout_out_defaults",
  "layout_defaults", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,    40,    41,    91,    93,
      44,    43,    45,    33,   126,    42,    47,    37,    60,    62,
      38,    94,   124,    63,    58,    61,    59,   123,   125
};
# endif

#define YYPACT_NINF -402

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-402)))

#define YYTABLE_NINF -286

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -41,   -14,    50,  -402,    -7,  -402,   -29,  -402,  -402,  -402,
    -402,   -26,   174,  1760,  -402,  -402,   -16,  -402,  -402,  -402,
      17,  -402,    41,    58,  -402,    62,  -402,  -402,  -402,  -402,
    -402,  -402,  -402,  -402,  -402,  -402,  -402,   -25,  -402,  -402,
    1898,  1898,  -402,  -402,  -402,   157,     4,    53,    57,    85,
      92,   -65,  -402,    46,  -402,  1681,  -402,    72,     9,    52,
     291,   -48,  -402,   204,  1959,  2020,  2020,    44,  2081,  2020,
    2081,  -402,    68,  -402,  2020,  -402,  -402,  -402,  -402,  -402,
     143,  -402,  -402,  -402,  -402,  -402,   174,  1837,    33,  -402,
    -402,  -402,  -402,  -402,  -402,  2020,  2020,  -402,  2020,  -402,
    2020,  2020,  -402,  -402,    44,  -402,  -402,  -402,  -402,  -402,
     -12,   174,  -402,  -402,  -402,   477,  -402,  -402,   540,   540,
    -402,  -402,  -402,   540,  -402,    44,   540,   540,   540,   174,
    -402,    84,    88,  -112,   100,   -30,   -28,   -23,   -22,  -402,
    -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,
    -402,  2081,  -402,  -402,   553,   114,  -402,   109,   186,   174,
     866,  -402,  1837,   125,  -402,  -402,  -402,   132,     8,  -402,
    -402,  -402,    34,   137,   138,  1197,   152,   160,   142,  1531,
     164,   169,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  1117,
    1117,  1117,  -402,  -402,  -402,  -402,  -402,   149,  -402,  -402,
    -402,    45,  -402,  -402,  -402,   171,    38,  1272,   178,   130,
    1117,   104,   115,   206,     6,   202,   165,   167,   170,   259,
     266,   -56,  -402,  -402,     3,  -402,   173,   185,  -402,  -402,
    -402,  -402,   633,  -402,  -402,  -402,  -402,  -402,  -402,  -402,
    -402,  -402,  -402,   174,  -402,  -402,  -402,  -110,   957,   -98,
    -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,   191,  -402,
     709,  1837,  -402,    68,    11,  -402,  -402,  -402,   945,  -402,
    1117,  -402,   -12,  -402,   174,  -402,  -402,   292,  1479,  1117,
    -402,  -402,    23,  1117,  1606,  -402,  -402,    48,  -402,  1197,
    -402,  -402,   280,  1117,  -402,  -402,  1117,   196,  -402,  -402,
    -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,
    -402,  1117,  -402,  1117,  1117,  1117,  1117,  1117,  1117,  1117,
    1117,  1117,  1117,  1117,  1117,  1117,  1117,  1117,  1117,  1117,
    1117,  1117,  1117,  1117,  -402,  -402,  -402,    68,   957,   -90,
     957,  -402,  -402,   957,  -402,  -402,   195,   174,   177,  1837,
     114,   174,  -402,  -402,  -402,  -402,  -402,   203,  -402,  -402,
    1606,    67,  -402,    71,   208,   174,   212,  -402,   789,  -402,
     213,   208,  -402,  -402,  -402,  -402,  -402,   104,   104,   115,
     115,   206,   206,   206,   206,     6,     6,   202,   165,   167,
     170,   259,   266,   -76,  -402,   114,  -402,   957,  -402,  -117,
    -402,  -402,    28,   300,  -402,  -402,  1117,  -402,   197,   214,
    1197,   199,   205,  1352,  -402,  -402,  1117,  -402,  1404,  -402,
    -402,    68,   198,    73,  1117,  1352,   351,  -402,    -8,  -402,
     957,  -402,  -402,  -402,  -402,  -402,  -402,   114,  -402,   217,
     208,  -402,  1197,  1117,   210,  -402,  -402,  1042,  1197,    -6,
    -402,  -402,  -402,   -47,  -402,  -402,  -402,  -402,  -402,  1197,
    -402
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       4,     0,     0,    12,     0,     1,     2,    14,    15,    16,
       5,     0,     0,     0,    13,     6,     0,   182,   181,   205,
     188,   178,   184,   185,   186,   187,   183,   179,   159,   158,
     157,   190,   191,   192,   193,   194,   189,     0,   204,   203,
     160,   161,   208,   207,   206,     0,     0,     0,     0,     0,
       0,     0,   180,   153,   275,     3,   274,     0,     0,   112,
     120,     0,   130,   135,   165,   167,   164,     0,   162,   163,
     166,   142,   199,   201,   168,   202,    18,   273,   109,   278,
       0,   301,   302,   303,   304,   276,     0,     0,     0,   188,
     184,   185,   187,    21,    22,   160,   161,   140,   165,   170,
     162,   166,   141,   169,     0,     7,     8,     9,    10,    11,
       0,     0,    20,    19,   106,     0,   277,   110,   120,   120,
     126,   127,   128,   120,   113,     0,   120,   120,   120,     0,
     107,    14,    16,   136,     0,   188,   184,   185,   187,   172,
     279,   293,   295,   297,   299,   173,   171,   143,   174,   286,
     175,   165,   177,   280,     0,   200,   176,     0,     0,     0,
       0,   211,     0,     0,   152,   151,   150,   147,     0,   145,
     149,   155,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    28,    29,    24,    25,    30,    26,    27,     0,
       0,     0,    54,    55,    56,    57,   241,   233,   237,    23,
      32,    50,    34,    39,    40,     0,     0,    44,     0,    58,
       0,    62,    65,    68,    73,    76,    78,    80,    82,    84,
      86,    88,    90,   103,     0,   223,     0,   142,   226,   239,
     225,   224,     0,   227,   228,   229,   230,   231,   114,   121,
     122,   118,   119,   129,   123,   125,   124,   131,     0,   137,
     115,   296,   298,   300,   294,   195,    58,   105,     0,    48,
       0,     0,    17,   216,     0,   214,   210,   212,     0,   108,
       0,   144,     0,   154,     0,   269,   268,     0,     0,     0,
     272,   270,     0,     0,     0,    51,    52,     0,   232,     0,
      36,    37,     0,     0,    42,    41,     0,   204,    45,    47,
      93,    94,    96,    95,    98,    99,   100,   101,   102,    97,
      92,     0,    53,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   242,   238,   240,   116,     0,   132,
       0,   218,   139,     0,   196,   197,     0,     0,     0,   290,
     217,     0,   213,   209,   148,   146,   156,     0,   263,   262,
     265,     0,   271,     0,   246,     0,     0,    31,     0,    35,
       0,    38,    46,    91,    59,    60,    61,    63,    64,    66,
      67,    71,    72,    69,    70,    74,    75,    77,    79,    81,
      83,    85,    87,     0,   104,   117,   134,     0,   221,     0,
     138,   198,     0,   287,   291,   215,     0,   264,     0,     0,
       0,     0,     0,     0,   234,    33,     0,   133,     0,   219,
     292,   288,     0,     0,   266,     0,   245,   243,     0,   248,
       0,   236,   259,   235,    89,   220,   222,   289,   281,     0,
     267,   261,     0,     0,     0,   249,   253,     0,   257,     0,
     247,   260,   244,     0,   252,   255,   254,   256,   250,   258,
     251
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -402,  -402,  -402,  -402,  -402,  -402,    10,   307,  -402,    95,
    -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,  -402,
     148,  -402,   -53,   -46,   -79,   -50,    43,    54,    42,    51,
      49,    55,  -402,  -147,   -34,  -402,  -123,  -233,    20,    25,
    -402,  -402,  -402,  -402,   265,    99,  -402,  -402,  -402,  -402,
     -81,   -11,  -402,   113,  -402,  -402,  -402,  -402,   276,    29,
    -402,    -9,  -132,   -13,  -402,  -402,   112,  -402,   225,  -157,
      47,    37,  -253,  -402,   117,  -156,  -401,  -402,  -402,   -35,
     334,   103,   120,  -402,  -402,    59,  -402,  -402,   -54,  -402,
     -44,  -402,  -402,  -402,  -402,  -402,  -402,   348,  -402,     2,
    -402,   338,  -402,    60,  -402,   343,   346,   347,   349,  -402
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     2,    13,     3,    54,     6,   263,    14,    55,   199,
     200,   201,   370,   202,   203,   204,   205,   206,   207,   208,
     209,   210,   211,   212,   213,   214,   215,   216,   217,   218,
     219,   220,   221,   222,   223,   311,   224,   258,   225,   226,
      58,    59,    60,   241,   124,   125,   126,   242,    61,    62,
      63,    98,   168,   169,   170,    65,   172,    66,    67,    68,
      69,   101,   155,   259,    72,    73,    74,    75,   160,   161,
     264,   265,   342,   399,   228,   229,   230,   231,   289,   432,
     433,   232,   233,   234,   427,   366,   235,   429,   446,   447,
     448,   449,   236,   360,   408,   409,   237,    76,    77,    78,
      79,    80,   422,   348,   349,    81,    82,    83,    84,    85
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      71,   249,    64,   267,    70,   331,   159,   257,   443,   444,
     443,   444,   431,  -285,    11,  -282,     7,     8,     9,   277,
    -283,  -284,    16,   418,   431,   164,   154,   346,   154,     7,
       8,     9,     4,    56,     7,     8,     9,   354,    57,     1,
     260,   419,    71,   248,    64,   338,    70,    88,   260,    19,
       5,   127,    12,    64,   147,    70,   282,   343,    15,   151,
    -285,    70,   320,   321,   333,   397,   140,    86,   287,   100,
     100,   110,   153,   133,    71,    56,   336,    10,   416,   159,
      57,   159,    37,    38,  -282,   396,    39,   398,   105,   165,
     400,   163,   129,   333,   100,   100,   158,   332,   100,   290,
     291,  -283,   227,   100,   151,  -284,    70,   460,   130,   127,
     127,   267,   243,   257,   127,   339,   100,   127,   127,   127,
     167,   171,   166,   257,   100,   100,   251,   100,   252,   100,
     100,   350,    87,   253,   254,    97,   102,   106,   292,   247,
     151,   107,    70,   333,   417,   271,   117,    71,   272,    71,
     445,   351,   458,   140,   322,   323,   361,   104,   134,   334,
     363,   364,   227,   333,   151,   436,    70,   352,   351,   108,
     371,   273,   128,   298,   274,   295,   109,   450,   296,   362,
     347,   -49,   111,   293,   420,   367,   157,   159,   333,   100,
     162,   100,   118,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   365,   410,   395,   154,   333,   411,   393,
     439,   333,   336,   333,   341,     7,     8,     9,   239,   227,
     -21,   151,   240,    70,   -22,   244,   245,   246,   114,   115,
     128,   128,    42,    43,    44,   128,   250,   364,   128,   128,
     128,   381,   382,   383,   384,   131,     8,   132,    71,   313,
     314,   315,   260,   337,   426,    71,   316,   317,   318,   319,
     324,   325,   372,   377,   378,   227,   261,   151,   347,    70,
     262,   227,   379,   380,   385,   386,   227,   373,   151,   365,
      70,   269,   167,   423,   356,   310,   452,   270,   278,   437,
     100,   455,   457,   275,   276,   119,   279,   100,   280,   394,
     283,   440,   256,   457,   341,   284,   341,   288,   294,   341,
     120,   121,   122,   100,   299,   326,    99,   103,   327,   329,
     453,   -48,   328,    31,    32,    33,    34,    35,   330,   114,
     344,   369,   357,   -43,   401,   403,    71,   285,   286,   406,
     139,   145,   146,   421,   148,   150,   152,   227,   333,   413,
     156,   425,   415,   424,   438,   227,   428,   151,   312,    70,
     430,   442,   112,   341,   454,   123,    42,    43,    44,   387,
     389,    99,   103,   451,   139,   412,   148,   152,   100,   391,
     390,   388,   434,   238,   341,   355,   392,   268,   405,   100,
     441,   116,   368,   456,   402,   358,   341,   227,   359,   151,
     227,    70,   151,   113,    70,   459,   149,   141,   256,   404,
     142,   143,   227,   144,   151,     0,    70,     0,   256,   407,
       0,     0,     0,     0,     0,     0,     0,   139,  -111,   227,
       0,   151,     0,    70,   227,   227,   151,   151,    70,    70,
       0,     0,     0,     0,     0,     0,   227,     0,   151,     0,
      70,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   374,   375,   376,   256,   256,   256,   256,   256,   256,
     256,   256,   256,   256,   256,   256,   256,   256,   256,   256,
      17,    18,    19,   173,    20,   174,   175,     0,   176,   177,
     178,   179,   180,     0,     0,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,     0,     0,     0,     0,    31,
      32,    33,    34,    35,    36,    37,    38,   181,    93,    39,
      94,   182,   183,   184,   185,   186,   187,   188,     0,     0,
       0,   189,   190,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   119,     0,     0,     0,     0,     0,
      40,    41,    42,    43,    44,     0,    45,     0,    19,   120,
     121,   122,     0,     0,     0,     0,     0,     0,     0,    51,
       0,     0,    31,    32,    33,    34,    35,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    37,    38,     0,    93,    39,    94,   182,   183,   184,
     185,   186,   187,   188,    52,    53,     0,   189,   190,     0,
       0,     0,     0,   191,   123,    42,    43,    44,   192,   193,
     194,   195,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   196,   197,   198,    17,    18,    19,   173,
      20,   174,   175,     0,   176,   177,   178,   179,   180,     0,
       0,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,     0,     0,     0,     0,    31,    32,    33,    34,    35,
      36,    37,    38,   181,    93,    39,    94,   182,   183,   184,
     185,   186,   187,   188,     0,     0,     0,   189,   190,   191,
       0,     0,   255,     0,   192,   193,   194,   195,     0,     0,
       0,     0,     0,     0,     0,     0,    40,    41,    42,    43,
      44,     0,    45,     0,    19,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    51,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    37,    38,     0,
      93,    39,    94,   182,   183,   184,   185,   186,   187,   188,
      52,    53,     0,   189,   190,     0,     0,     0,     0,   191,
       0,     0,     0,     0,   192,   193,   194,   195,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   196,
     197,   335,    17,    18,    19,   173,    20,   174,   175,     0,
     176,   177,   178,   179,   180,     0,     0,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,     0,     0,     0,
       0,    31,    32,    33,    34,    35,    36,    37,    38,   181,
      93,    39,    94,   182,   183,   184,   185,   186,   187,   188,
       0,     0,     0,   189,   190,   191,     0,     0,   345,     0,
     192,   193,   194,   195,     0,     0,     0,     0,     0,     0,
       0,     0,    40,    41,    42,    43,    44,     0,    45,    17,
      18,    19,     0,    89,     0,     0,     0,     0,     0,     0,
       0,    51,     0,     0,    21,    90,    91,    24,    92,    26,
      27,    28,    29,    30,     0,     0,     0,     0,    31,    32,
      33,    34,    35,    36,    37,    38,     0,     0,    39,     0,
       0,     0,     0,     0,     0,     0,    52,    53,     0,     0,
       0,     0,     0,     0,     0,   191,     0,     0,     0,     0,
     192,   193,   194,   195,     0,     0,     0,     0,     0,    95,
      96,    42,    43,    44,     0,   196,   197,   414,    17,    18,
      19,     0,    89,     0,     0,     0,     0,     0,    51,     0,
       0,     0,    19,    21,    90,    91,    24,    92,    26,    27,
      28,    29,    30,     0,     0,     0,     0,    31,    32,    33,
      34,    35,    36,    37,    38,     0,     0,    39,     0,     0,
       0,     0,     0,    52,    53,    37,    38,     0,    93,    39,
      94,   182,   183,   184,   185,   186,   187,   188,     0,     0,
       0,   189,   190,     0,     0,     0,     0,     0,    95,    96,
      42,    43,    44,     0,   266,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    51,     0,     0,
       0,     0,     0,     0,     0,    17,    18,    19,   173,    20,
     174,   175,     0,   176,   177,   178,   179,   180,   443,   444,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
       0,     0,    52,    53,    31,    32,    33,    34,    35,    36,
      37,    38,   181,    93,    39,    94,   182,   183,   184,   185,
     186,   187,   188,   191,     0,     0,   189,   190,   192,   193,
     194,   195,     0,   353,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   340,    40,    41,    42,    43,    44,
       0,    45,    19,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    51,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    37,    38,     0,    93,    39,
      94,   182,   183,   184,   185,   186,   187,   188,     0,    52,
      53,   189,   190,     0,     0,     0,     0,     0,   191,     0,
       0,     0,     0,   192,   193,   194,   195,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   196,   197,
      17,    18,    19,   173,    20,   174,   175,     0,   176,   177,
     178,   179,   180,     0,     0,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,     0,     0,     0,     0,    31,
      32,    33,    34,    35,    36,    37,    38,   181,    93,    39,
      94,   182,   183,   184,   185,   186,   187,   188,     0,     0,
       0,   189,   190,   191,     0,     0,     0,     0,   192,   193,
     194,   195,     0,     0,     0,     0,     0,     0,     0,     0,
      40,    41,    42,    43,    44,     0,    45,    19,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    51,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      37,   297,     0,    93,    39,    94,   182,   183,   184,   185,
     186,   187,   188,     0,    52,    53,   189,   190,     0,     0,
       0,     0,     0,   191,     0,     0,     0,     0,   192,   193,
     194,   195,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   196,   197,    17,    18,    19,   173,    20,
     174,   175,     0,   176,   177,   178,   179,   180,     0,     0,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
       0,     0,     0,     0,    31,    32,    33,    34,    35,    36,
      37,    38,   181,    93,    39,    94,   182,   183,   184,   185,
     186,   187,   188,     0,     0,     0,   189,   190,   191,    19,
       0,     0,     0,   192,   193,   194,   195,     0,     0,     0,
       0,     0,     0,     0,     0,    40,    41,    42,    43,    44,
       0,    45,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    37,    38,    51,    93,    39,    94,   182,   183,
     184,   185,   186,   187,   188,     0,     0,     0,   189,   190,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    52,
      53,     0,    17,    18,    19,     0,    20,     0,   191,     0,
       0,     0,     0,   192,   193,   194,   195,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,     0,   196,   115,
       0,    31,    32,    33,    34,    35,    36,    37,    38,     0,
      93,    39,    94,   182,   183,   184,   185,   186,   187,   188,
       0,     0,     0,   189,   190,     0,    19,     0,     0,     0,
     191,     0,     0,     0,     0,   192,   193,   194,   195,     0,
       0,     0,    40,    41,    42,    43,    44,     0,    45,     0,
       0,   340,   435,     0,     0,     0,     0,     0,     0,    37,
      38,    51,    93,    39,    94,   182,   183,   184,   185,   186,
     187,   188,     0,     0,     0,   189,   190,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    52,    53,     0,    17,
      18,    19,     0,    89,     0,   191,     0,     0,     0,     0,
     192,   193,   194,   195,    21,    90,    91,    24,    92,    26,
      27,    28,    29,    30,     0,   196,     0,     0,    31,    32,
      33,    34,    35,    36,    37,    38,     0,    93,    39,    94,
     182,   183,   184,   185,   186,   187,   188,     0,     0,     0,
     189,   190,     0,     0,     0,     0,     0,   191,     0,     0,
       0,     0,   192,   193,   194,   195,     0,     0,     0,    95,
      96,    42,    43,    44,    17,    18,    19,   281,    20,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    51,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,     0,
       0,     0,     0,    31,    32,    33,    34,    35,    36,    37,
      38,     0,     0,    39,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    52,    53,     0,     0,     0,     0,     0,
       0,     0,   191,     0,     0,     0,     0,   192,   193,   194,
     195,     0,     0,     0,    40,    41,    42,    43,    44,     0,
      45,     0,    12,    17,    18,    19,     0,    20,    46,    47,
      48,    49,    50,    51,     0,     0,     0,     0,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,     0,     0,
       0,     0,    31,    32,    33,    34,    35,    36,    37,    38,
       0,     0,    39,     0,     0,     0,     0,     0,    52,    53,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    40,    41,    42,    43,    44,     0,    45,
      17,    18,    19,     0,    89,     0,     0,    46,    47,    48,
      49,    50,    51,     0,     0,    21,    90,    91,    24,    92,
      26,    27,    28,    29,    30,     0,     0,     0,     0,    31,
      32,    33,    34,    35,    36,    37,    38,     0,     0,    39,
       0,     0,     0,     0,     0,     0,     0,    52,    53,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    17,    18,     0,     0,    89,     0,     0,     0,     0,
      95,    96,    42,    43,    44,     0,    21,    90,    91,    24,
      92,    26,    27,    28,    29,    30,     0,     0,     0,    51,
      31,    32,    33,    34,    35,    36,     0,     0,     0,    93,
       0,    94,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    17,    18,    52,    53,   135,     0,     0,     0,
       0,    95,    96,    42,    43,    44,     0,    21,   136,   137,
      24,   138,    26,    27,    28,    29,    30,     0,     0,     0,
      51,    31,    32,    33,    34,    35,    36,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    17,    18,    52,    53,    89,     0,     0,
       0,     0,    95,    96,    42,    43,    44,     0,    21,    90,
      91,    24,    92,    26,    27,    28,    29,    30,     0,     0,
       0,    51,    31,    32,    33,    34,    35,    36,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    17,    18,    52,    53,    20,     0,
       0,     0,     0,    95,    96,    42,    43,    44,     0,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,     0,
       0,     0,    51,    31,    32,    33,    34,    35,    36,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    52,    53,     0,
       0,     0,     0,     0,    95,    96,    42,    43,    44,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    51,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    52,    53
};

static const yytype_int16 yycheck[] =
{
      13,   133,    13,   160,    13,    61,    87,   154,    16,    17,
      16,    17,   413,    43,     4,    43,    41,    42,    43,   175,
      43,    43,    12,   140,   425,    37,   138,   260,   138,    41,
      42,    43,    46,    13,    41,    42,    43,   270,    13,    80,
     138,   158,    55,   155,    55,   155,    55,    37,   138,     5,
       0,    60,    81,    64,    67,    64,   179,   155,    84,    70,
      43,    70,    56,    57,   140,   155,    64,    83,   191,    40,
      41,   136,    70,    63,    87,    55,   232,    84,   154,   160,
      55,   162,    38,    39,    43,   338,    42,   340,    84,   101,
     343,   104,   140,   140,    65,    66,    86,   153,    69,    54,
      55,    43,   115,    74,   115,    43,   115,   154,   156,   118,
     119,   268,   125,   260,   123,   247,    87,   126,   127,   128,
     110,   111,   134,   270,    95,    96,   156,    98,   156,   100,
     101,   263,   157,   156,   156,    40,    41,    84,    93,   129,
     151,    84,   151,   140,   397,   137,   137,   160,   140,   162,
     158,   140,   158,   151,   148,   149,   279,    45,    63,   156,
     283,   284,   175,   140,   175,   418,   175,   156,   140,    84,
     293,   137,    60,   207,   140,   137,    84,   430,   140,   156,
     261,   136,   136,   138,   156,   137,    43,   268,   140,   160,
     157,   162,   140,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,   284,   137,   337,   138,   140,   137,   332,
     137,   140,   368,   140,   248,    41,    42,    43,   119,   232,
     136,   232,   123,   232,   136,   126,   127,   128,   156,   157,
     118,   119,    75,    76,    77,   123,   136,   360,   126,   127,
     128,   320,   321,   322,   323,    41,    42,    43,   261,   145,
     146,   147,   138,   243,   410,   268,   141,   142,    52,    53,
      58,    59,   296,   316,   317,   278,   157,   278,   349,   278,
      84,   284,   318,   319,   324,   325,   289,   311,   289,   360,
     289,   156,   272,   406,   274,   155,   442,   155,   136,   421,
     261,   447,   448,   156,   156,     4,   136,   268,   156,   333,
     136,   424,   154,   459,   338,   136,   340,   158,   137,   343,
      19,    20,    21,   284,   136,   150,    40,    41,   151,    60,
     443,   136,   152,    32,    33,    34,    35,    36,    62,   156,
     139,    51,    40,   137,   139,   158,   349,   189,   190,   136,
      64,    65,    66,    43,    68,    69,    70,   360,   140,   137,
      74,   137,   139,   156,   156,   368,   157,   368,   210,   368,
     155,    10,    55,   397,   154,    74,    75,    76,    77,   326,
     328,    95,    96,   156,    98,   365,   100,   101,   349,   330,
     329,   327,   416,   118,   418,   272,   331,   162,   351,   360,
     425,    57,   289,   447,   347,   278,   430,   410,   278,   410,
     413,   410,   413,    55,   413,   449,    68,    64,   260,   349,
      64,    64,   425,    64,   425,    -1,   425,    -1,   270,   360,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   151,   137,   442,
      -1,   442,    -1,   442,   447,   448,   447,   448,   447,   448,
      -1,    -1,    -1,    -1,    -1,    -1,   459,    -1,   459,    -1,
     459,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   313,   314,   315,   316,   317,   318,   319,   320,   321,
     322,   323,   324,   325,   326,   327,   328,   329,   330,   331,
       3,     4,     5,     6,     7,     8,     9,    -1,    11,    12,
      13,    14,    15,    -1,    -1,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    -1,    -1,    -1,    -1,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    -1,    -1,
      -1,    54,    55,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     4,    -1,    -1,    -1,    -1,    -1,
      73,    74,    75,    76,    77,    -1,    79,    -1,     5,    19,
      20,    21,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    92,
      -1,    -1,    32,    33,    34,    35,    36,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    38,    39,    -1,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,   127,   128,    -1,    54,    55,    -1,
      -1,    -1,    -1,   136,    74,    75,    76,    77,   141,   142,
     143,   144,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   156,   157,   158,     3,     4,     5,     6,
       7,     8,     9,    -1,    11,    12,    13,    14,    15,    -1,
      -1,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    -1,    -1,    -1,    -1,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    -1,    -1,    -1,    54,    55,   136,
      -1,    -1,   139,    -1,   141,   142,   143,   144,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    73,    74,    75,    76,
      77,    -1,    79,    -1,     5,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    92,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    38,    39,    -1,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
     127,   128,    -1,    54,    55,    -1,    -1,    -1,    -1,   136,
      -1,    -1,    -1,    -1,   141,   142,   143,   144,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   156,
     157,   158,     3,     4,     5,     6,     7,     8,     9,    -1,
      11,    12,    13,    14,    15,    -1,    -1,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    -1,    -1,    -1,
      -1,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      -1,    -1,    -1,    54,    55,   136,    -1,    -1,   139,    -1,
     141,   142,   143,   144,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    73,    74,    75,    76,    77,    -1,    79,     3,
       4,     5,    -1,     7,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    92,    -1,    -1,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    -1,    -1,    -1,    -1,    32,    33,
      34,    35,    36,    37,    38,    39,    -1,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   127,   128,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   136,    -1,    -1,    -1,    -1,
     141,   142,   143,   144,    -1,    -1,    -1,    -1,    -1,    73,
      74,    75,    76,    77,    -1,   156,   157,   158,     3,     4,
       5,    -1,     7,    -1,    -1,    -1,    -1,    -1,    92,    -1,
      -1,    -1,     5,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    -1,    -1,    -1,    -1,    32,    33,    34,
      35,    36,    37,    38,    39,    -1,    -1,    42,    -1,    -1,
      -1,    -1,    -1,   127,   128,    38,    39,    -1,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    -1,    -1,
      -1,    54,    55,    -1,    -1,    -1,    -1,    -1,    73,    74,
      75,    76,    77,    -1,   158,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    92,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,
       8,     9,    -1,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      -1,    -1,   127,   128,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,   136,    -1,    -1,    54,    55,   141,   142,
     143,   144,    -1,   158,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   157,    73,    74,    75,    76,    77,
      -1,    79,     5,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    38,    39,    -1,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    -1,   127,
     128,    54,    55,    -1,    -1,    -1,    -1,    -1,   136,    -1,
      -1,    -1,    -1,   141,   142,   143,   144,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   156,   157,
       3,     4,     5,     6,     7,     8,     9,    -1,    11,    12,
      13,    14,    15,    -1,    -1,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    -1,    -1,    -1,    -1,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    -1,    -1,
      -1,    54,    55,   136,    -1,    -1,    -1,    -1,   141,   142,
     143,   144,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      73,    74,    75,    76,    77,    -1,    79,     5,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    92,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      38,    39,    -1,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,   127,   128,    54,    55,    -1,    -1,
      -1,    -1,    -1,   136,    -1,    -1,    -1,    -1,   141,   142,
     143,   144,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   156,   157,     3,     4,     5,     6,     7,
       8,     9,    -1,    11,    12,    13,    14,    15,    -1,    -1,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      -1,    -1,    -1,    -1,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    -1,    -1,    -1,    54,    55,   136,     5,
      -1,    -1,    -1,   141,   142,   143,   144,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    73,    74,    75,    76,    77,
      -1,    79,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    38,    39,    92,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    -1,    -1,    -1,    54,    55,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   127,
     128,    -1,     3,     4,     5,    -1,     7,    -1,   136,    -1,
      -1,    -1,    -1,   141,   142,   143,   144,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    -1,   156,   157,
      -1,    32,    33,    34,    35,    36,    37,    38,    39,    -1,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      -1,    -1,    -1,    54,    55,    -1,     5,    -1,    -1,    -1,
     136,    -1,    -1,    -1,    -1,   141,   142,   143,   144,    -1,
      -1,    -1,    73,    74,    75,    76,    77,    -1,    79,    -1,
      -1,   157,   158,    -1,    -1,    -1,    -1,    -1,    -1,    38,
      39,    92,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    -1,    -1,    -1,    54,    55,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   127,   128,    -1,     3,
       4,     5,    -1,     7,    -1,   136,    -1,    -1,    -1,    -1,
     141,   142,   143,   144,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    -1,   156,    -1,    -1,    32,    33,
      34,    35,    36,    37,    38,    39,    -1,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    -1,    -1,    -1,
      54,    55,    -1,    -1,    -1,    -1,    -1,   136,    -1,    -1,
      -1,    -1,   141,   142,   143,   144,    -1,    -1,    -1,    73,
      74,    75,    76,    77,     3,     4,     5,   156,     7,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    92,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    -1,
      -1,    -1,    -1,    32,    33,    34,    35,    36,    37,    38,
      39,    -1,    -1,    42,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   127,   128,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   136,    -1,    -1,    -1,    -1,   141,   142,   143,
     144,    -1,    -1,    -1,    73,    74,    75,    76,    77,    -1,
      79,    -1,    81,     3,     4,     5,    -1,     7,    87,    88,
      89,    90,    91,    92,    -1,    -1,    -1,    -1,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    -1,    -1,
      -1,    -1,    32,    33,    34,    35,    36,    37,    38,    39,
      -1,    -1,    42,    -1,    -1,    -1,    -1,    -1,   127,   128,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    73,    74,    75,    76,    77,    -1,    79,
       3,     4,     5,    -1,     7,    -1,    -1,    87,    88,    89,
      90,    91,    92,    -1,    -1,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    -1,    -1,    -1,    -1,    32,
      33,    34,    35,    36,    37,    38,    39,    -1,    -1,    42,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   127,   128,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,     3,     4,    -1,    -1,     7,    -1,    -1,    -1,    -1,
      73,    74,    75,    76,    77,    -1,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    -1,    -1,    -1,    92,
      32,    33,    34,    35,    36,    37,    -1,    -1,    -1,    41,
      -1,    43,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,     3,     4,   127,   128,     7,    -1,    -1,    -1,
      -1,    73,    74,    75,    76,    77,    -1,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    -1,    -1,    -1,
      92,    32,    33,    34,    35,    36,    37,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     3,     4,   127,   128,     7,    -1,    -1,
      -1,    -1,    73,    74,    75,    76,    77,    -1,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    -1,    -1,
      -1,    92,    32,    33,    34,    35,    36,    37,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,   127,   128,     7,    -1,
      -1,    -1,    -1,    73,    74,    75,    76,    77,    -1,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    -1,
      -1,    -1,    92,    32,    33,    34,    35,    36,    37,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   127,   128,    -1,
      -1,    -1,    -1,    -1,    73,    74,    75,    76,    77,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   127,   128
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint16 yystos[] =
{
       0,    80,   160,   162,    46,     0,   164,    41,    42,    43,
      84,   165,    81,   161,   166,    84,   165,     3,     4,     5,
       7,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    32,    33,    34,    35,    36,    37,    38,    39,    42,
      73,    74,    75,    76,    77,    79,    87,    88,    89,    90,
      91,    92,   127,   128,   163,   167,   197,   198,   199,   200,
     201,   207,   208,   209,   210,   214,   216,   217,   218,   219,
     220,   222,   223,   224,   225,   226,   256,   257,   258,   259,
     260,   264,   265,   266,   267,   268,    83,   157,   165,     7,
      19,    20,    22,    41,    43,    73,    74,   168,   210,   217,
     218,   220,   168,   217,   225,    84,    84,    84,    84,    84,
     136,   136,   166,   256,   156,   157,   239,   137,   140,     4,
      19,    20,    21,    74,   203,   204,   205,   220,   225,   140,
     156,    41,    43,   165,   168,     7,    19,    20,    22,   217,
     258,   264,   265,   266,   267,   217,   217,   222,   217,   260,
     217,   210,   217,   258,   138,   221,   217,    43,   165,   209,
     227,   228,   157,   222,    37,   101,   134,   165,   211,   212,
     213,   165,   215,     6,     8,     9,    11,    12,    13,    14,
      15,    40,    44,    45,    46,    47,    48,    49,    50,    54,
      55,   136,   141,   142,   143,   144,   156,   157,   158,   168,
     169,   170,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,   185,   186,   187,   188,   189,
     190,   191,   192,   193,   195,   197,   198,   222,   233,   234,
     235,   236,   240,   241,   242,   245,   251,   255,   203,   204,
     204,   202,   206,   222,   204,   204,   204,   165,   155,   221,
     136,   156,   156,   156,   156,   139,   179,   192,   196,   222,
     138,   157,    84,   165,   229,   230,   158,   228,   227,   156,
     155,   137,   140,   137,   140,   156,   156,   234,   136,   136,
     156,   156,   195,   136,   136,   179,   179,   195,   158,   237,
      54,    55,    93,   138,   137,   137,   140,    39,   193,   136,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
     155,   194,   179,   145,   146,   147,   141,   142,    52,    53,
      56,    57,   148,   149,    58,    59,   150,   151,   152,    60,
      62,    61,   153,   140,   156,   158,   234,   165,   155,   221,
     157,   193,   231,   155,   139,   139,   196,   209,   262,   263,
     221,   140,   156,   158,   196,   212,   165,    40,   233,   241,
     252,   195,   156,   195,   195,   209,   244,   137,   240,    51,
     171,   195,   193,   193,   179,   179,   179,   181,   181,   182,
     182,   183,   183,   183,   183,   184,   184,   185,   186,   187,
     188,   189,   190,   195,   193,   221,   231,   155,   231,   232,
     231,   139,   229,   158,   262,   230,   136,   244,   253,   254,
     137,   137,   165,   137,   158,   139,   154,   231,   140,   158,
     156,    43,   261,   195,   156,   137,   234,   243,   157,   246,
     155,   235,   238,   239,   193,   158,   231,   221,   156,   137,
     195,   238,    10,    16,    17,   158,   247,   248,   249,   250,
     231,   156,   234,   195,   154,   234,   247,   234,   158,   249,
     154
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint16 yyr1[] =
{
       0,   159,   161,   160,   162,   162,   162,   163,   163,   163,
     163,   163,   164,   164,   165,   165,   165,   166,   167,   167,
     167,   168,   168,   169,   169,   169,   169,   169,   169,   169,
     169,   169,   170,   170,   170,   170,   170,   170,   171,   172,
     173,   174,   174,   175,   175,   176,   176,   177,   178,   178,
     179,   179,   179,   179,   180,   180,   180,   180,   181,   181,
     181,   181,   182,   182,   182,   183,   183,   183,   184,   184,
     184,   184,   184,   185,   185,   185,   186,   186,   187,   187,
     188,   188,   189,   189,   190,   190,   191,   191,   192,   192,
     193,   193,   194,   194,   194,   194,   194,   194,   194,   194,
     194,   194,   194,   195,   195,   196,   197,   197,   197,   197,
     198,   199,   199,   200,   200,   201,   202,   202,   203,   203,
     204,   204,   204,   204,   204,   204,   205,   205,   205,   206,
     207,   207,   207,   207,   207,   208,   208,   208,   208,   208,
     208,   208,   209,   209,   210,   211,   211,   212,   212,   212,
     213,   213,   213,   214,   214,   215,   215,   216,   216,   216,
     217,   217,   217,   217,   217,   217,   217,   217,   217,   217,
     217,   217,   217,   217,   217,   217,   217,   217,   218,   218,
     218,   219,   219,   219,   219,   219,   219,   219,   219,   219,
     220,   220,   220,   220,   220,   221,   221,   221,   221,   222,
     222,   223,   223,   223,   224,   224,   225,   225,   225,   226,
     226,   227,   227,   228,   229,   229,   230,   230,   231,   231,
     231,   232,   232,   233,   234,   234,   235,   235,   235,   235,
     235,   235,   236,   237,   236,   238,   238,   239,   239,   240,
     240,   241,   241,   242,   243,   243,   244,   244,   245,   246,
     246,   247,   247,   248,   248,   249,   249,   250,   250,   251,
     251,   251,   252,   252,   253,   253,   254,   254,   255,   255,
     255,   255,   255,   256,   256,   256,   256,   257,   258,   258,
     258,   259,   260,   260,   260,   260,   260,   261,   261,   261,
     262,   262,   263,   264,   264,   265,   265,   266,   266,   267,
     267,   268,   268,   268,   268
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     4,     0,     3,     4,     2,     2,     2,
       2,     2,     0,     2,     1,     1,     1,     5,     1,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     3,     1,     4,     1,     3,     2,     2,     1,     1,
       1,     2,     2,     2,     1,     2,     3,     2,     1,     1,
       1,     2,     2,     2,     1,     1,     1,     1,     1,     3,
       3,     3,     1,     3,     3,     1,     3,     3,     1,     3,
       3,     3,     3,     1,     3,     3,     1,     3,     1,     3,
       1,     3,     1,     3,     1,     3,     1,     3,     1,     5,
       1,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     2,     2,     4,     1,
       2,     1,     1,     2,     3,     3,     2,     3,     2,     2,
       0,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     3,     4,     6,     5,     1,     2,     3,     5,     4,
       2,     2,     1,     2,     4,     1,     3,     1,     3,     1,
       1,     1,     1,     1,     4,     1,     3,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     3,     3,     4,     1,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     5,
       4,     1,     2,     3,     1,     3,     1,     2,     1,     3,
       4,     1,     3,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     0,     4,     1,     1,     2,     3,     1,
       2,     1,     2,     5,     3,     1,     1,     4,     5,     2,
       3,     3,     2,     1,     2,     2,     2,     1,     2,     5,
       7,     6,     1,     1,     1,     0,     2,     3,     2,     2,
       2,     3,     2,     1,     1,     1,     1,     2,     1,     2,
       2,     7,     1,     1,     1,     1,     2,     0,     1,     2,
       1,     2,     3,     2,     3,     2,     3,     2,     3,     2,
       3,     1,     1,     1,     1
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
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
      yyerror (&yylloc, state, YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


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


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static unsigned
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  unsigned res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
 }

#  define YY_LOCATION_PRINT(File, Loc)          \
  yy_location_print_ (File, &(Loc))

# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value, Location, state); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct _mesa_glsl_parse_state *state)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  YYUSE (yylocationp);
  YYUSE (state);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct _mesa_glsl_parse_state *state)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
  yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp, state);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
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
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule, struct _mesa_glsl_parse_state *state)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                       , &(yylsp[(yyi + 1) - (yynrhs)])                       , state);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule, state); \
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
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
yystrlen (const char *yystr)
{
  YYSIZE_T yylen;
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
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
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
            /* Fall through.  */
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

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
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
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

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
      int yyn = yypact[*yyssp];
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
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
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
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
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
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp, struct _mesa_glsl_parse_state *state)
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);
  YYUSE (state);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/*----------.
| yyparse.  |
`----------*/

int
yyparse (struct _mesa_glsl_parse_state *state)
{
/* The lookahead symbol.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

/* Location data for the lookahead symbol.  */
static YYLTYPE yyloc_default
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
YYLTYPE yylloc = yyloc_default;

    /* Number of syntax errors so far.  */
    int yynerrs;

    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.
       'yyls': related to locations.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    /* The location stack.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls;
    YYLTYPE *yylsp;

    /* The locations where the error started and ended.  */
    YYLTYPE yyerror_range[3];

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yylsp = yyls = yylsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */

/* User initialization code.  */
#line 87 "./glsl/glsl_parser.yy" /* yacc.c:1429  */
{
   yylloc.first_line = 1;
   yylloc.first_column = 1;
   yylloc.last_line = 1;
   yylloc.last_column = 1;
   yylloc.source = 0;
}

#line 2173 "glsl/glsl_parser.cpp" /* yacc.c:1429  */
  yylsp[0] = yylloc;
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        YYSTYPE *yyvs1 = yyvs;
        yytype_int16 *yyss1 = yyss;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * sizeof (*yyssp),
                    &yyvs1, yysize * sizeof (*yyvsp),
                    &yyls1, yysize * sizeof (*yylsp),
                    &yystacksize);

        yyls = yyls1;
        yyss = yyss1;
        yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yytype_int16 *yyss1 = yyss;
        union yyalloc *yyptr =
          (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
                  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

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
      yychar = yylex (&yylval, &yylloc, state);
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

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;
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
| yyreduce -- Do a reduction.  |
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

  /* Default location.  */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 289 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      _mesa_glsl_initialize_types(state);
   }
#line 2364 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 3:
#line 293 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      delete state->symbols;
      state->symbols = new(ralloc_parent(state)) glsl_symbol_table;
      if (state->es_shader) {
         if (state->stage == MESA_SHADER_FRAGMENT) {
            state->symbols->add_default_precision_qualifier("int", ast_precision_medium);
         } else {
            state->symbols->add_default_precision_qualifier("float", ast_precision_high);
            state->symbols->add_default_precision_qualifier("int", ast_precision_high);
         }
         state->symbols->add_default_precision_qualifier("sampler2D", ast_precision_low);
         state->symbols->add_default_precision_qualifier("samplerExternalOES", ast_precision_low);
         state->symbols->add_default_precision_qualifier("samplerCube", ast_precision_low);
         state->symbols->add_default_precision_qualifier("atomic_uint", ast_precision_high);
      }
      _mesa_glsl_initialize_types(state);
   }
#line 2386 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 5:
#line 315 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->process_version_directive(&(yylsp[-1]), (yyvsp[-1].n), NULL);
      if (state->error) {
         YYERROR;
      }
   }
#line 2397 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 6:
#line 322 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->process_version_directive(&(yylsp[-2]), (yyvsp[-2].n), (yyvsp[-1].identifier));
      if (state->error) {
         YYERROR;
      }
   }
#line 2408 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 11:
#line 336 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* Pragma invariant(all) cannot be used in a fragment shader.
       *
       * Page 27 of the GLSL 1.20 spec, Page 53 of the GLSL ES 3.00 spec:
       *
       *     "It is an error to use this pragma in a fragment shader."
       */
      if (state->is_version(120, 300) &&
          state->stage == MESA_SHADER_FRAGMENT) {
         _mesa_glsl_error(& (yylsp[-1]), state,
                          "pragma `invariant(all)' cannot be used "
                          "in a fragment shader.");
      } else if (!state->is_version(120, 100)) {
         _mesa_glsl_warning(& (yylsp[-1]), state,
                            "pragma `invariant(all)' not supported in %s "
                            "(GLSL ES 1.00 or GLSL 1.20 required)",
                            state->get_version_string());
      } else {
         state->all_invariant = true;
      }
   }
#line 2434 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 17:
#line 372 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!_mesa_glsl_process_extension((yyvsp[-3].identifier), & (yylsp[-3]), (yyvsp[-1].identifier), & (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 2444 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 18:
#line 381 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* FINISHME: The NULL test is required because pragmas are set to
       * FINISHME: NULL. (See production rule for external_declaration.)
       */
      if ((yyvsp[0].node) != NULL)
         state->translation_unit.push_tail(& (yyvsp[0].node)->link);
   }
#line 2456 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 19:
#line 389 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* FINISHME: The NULL test is required because pragmas are set to
       * FINISHME: NULL. (See production rule for external_declaration.)
       */
      if ((yyvsp[0].node) != NULL)
         state->translation_unit.push_tail(& (yyvsp[0].node)->link);
   }
#line 2468 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 20:
#line 396 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!state->allow_extension_directive_midshader) {
         _mesa_glsl_error(& (yylsp[0]), state,
                          "#extension directive is not allowed "
                          "in the middle of a shader");
         YYERROR;
      }
   }
#line 2481 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 23:
#line 413 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_identifier, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.identifier = (yyvsp[0].identifier);
   }
#line 2492 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 24:
#line 420 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_int_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.int_constant = (yyvsp[0].n);
   }
#line 2503 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 25:
#line 427 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_uint_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.uint_constant = (yyvsp[0].n);
   }
#line 2514 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 26:
#line 434 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_int64_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.int64_constant = (yyvsp[0].n64);
   }
#line 2525 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 27:
#line 441 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_uint64_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.uint64_constant = (yyvsp[0].n64);
   }
#line 2536 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 28:
#line 448 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_float_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.float_constant = (yyvsp[0].real);
   }
#line 2547 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 29:
#line 455 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_double_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.double_constant = (yyvsp[0].dreal);
   }
#line 2558 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 30:
#line 462 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_bool_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.bool_constant = (yyvsp[0].n);
   }
#line 2569 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 31:
#line 469 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
   }
#line 2577 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 33:
#line 477 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_array_index, (yyvsp[-3].expression), (yyvsp[-1].expression), NULL);
      (yyval.expression)->set_location_range((yylsp[-3]), (yylsp[0]));
   }
#line 2587 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 34:
#line 483 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[0].expression);
   }
#line 2595 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 35:
#line 487 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_field_selection, (yyvsp[-2].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.expression)->primary_expression.identifier = (yyvsp[0].identifier);
   }
#line 2606 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 36:
#line 494 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_post_inc, (yyvsp[-1].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 2616 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 37:
#line 500 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_post_dec, (yyvsp[-1].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 2626 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 45:
#line 531 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
      (yyval.expression)->set_location((yylsp[-1]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 2636 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 46:
#line 537 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-2].expression);
      (yyval.expression)->set_location((yylsp[-2]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 2646 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 48:
#line 553 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_function_expression((yyvsp[0].type_specifier));
      (yyval.expression)->set_location((yylsp[0]));
      }
#line 2656 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 49:
#line 559 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_function_expression((yyvsp[0].expression));
      (yyval.expression)->set_location((yylsp[0]));
      }
#line 2666 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 51:
#line 574 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_pre_inc, (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location((yylsp[-1]));
   }
#line 2676 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 52:
#line 580 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_pre_dec, (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location((yylsp[-1]));
   }
#line 2686 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 53:
#line 586 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression((yyvsp[-1].n), (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 2696 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 54:
#line 595 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_plus; }
#line 2702 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 55:
#line 596 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_neg; }
#line 2708 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 56:
#line 597 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_logic_not; }
#line 2714 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 57:
#line 598 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_bit_not; }
#line 2720 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 59:
#line 604 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_mul, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2730 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 60:
#line 610 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_div, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2740 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 61:
#line 616 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_mod, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2750 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 63:
#line 626 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_add, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2760 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 64:
#line 632 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_sub, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2770 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 66:
#line 642 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_lshift, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2780 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 67:
#line 648 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_rshift, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2790 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 69:
#line 658 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_less, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2800 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 70:
#line 664 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_greater, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2810 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 71:
#line 670 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_lequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2820 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 72:
#line 676 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_gequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2830 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 74:
#line 686 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_equal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2840 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 75:
#line 692 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_nequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2850 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 77:
#line 702 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_and, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2860 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 79:
#line 712 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_xor, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2870 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 81:
#line 722 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_or, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2880 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 83:
#line 732 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_and, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2890 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 85:
#line 742 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_xor, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2900 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 87:
#line 752 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_or, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2910 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 89:
#line 762 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_conditional, (yyvsp[-4].expression), (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 2920 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 91:
#line 772 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression((yyvsp[-1].n), (yyvsp[-2].expression), (yyvsp[0].expression), NULL);
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 2930 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 92:
#line 780 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_assign; }
#line 2936 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 93:
#line 781 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_mul_assign; }
#line 2942 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 94:
#line 782 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_div_assign; }
#line 2948 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 95:
#line 783 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_mod_assign; }
#line 2954 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 96:
#line 784 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_add_assign; }
#line 2960 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 97:
#line 785 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_sub_assign; }
#line 2966 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 98:
#line 786 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_ls_assign; }
#line 2972 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 99:
#line 787 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_rs_assign; }
#line 2978 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 100:
#line 788 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_and_assign; }
#line 2984 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 101:
#line 789 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_xor_assign; }
#line 2990 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 102:
#line 790 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_or_assign; }
#line 2996 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 103:
#line 795 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[0].expression);
   }
#line 3004 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 104:
#line 799 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      if ((yyvsp[-2].expression)->oper != ast_sequence) {
         (yyval.expression) = new(ctx) ast_expression(ast_sequence, NULL, NULL, NULL);
         (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
         (yyval.expression)->expressions.push_tail(& (yyvsp[-2].expression)->link);
      } else {
         (yyval.expression) = (yyvsp[-2].expression);
      }

      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 3021 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 106:
#line 819 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->symbols->pop_scope();
      (yyval.node) = (yyvsp[-1].function);
   }
#line 3030 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 107:
#line 824 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[-1].declarator_list);
   }
#line 3038 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 108:
#line 828 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyvsp[-1].type_specifier)->default_precision = (yyvsp[-2].n);
      (yyval.node) = (yyvsp[-1].type_specifier);
   }
#line 3047 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 109:
#line 833 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *block = (ast_interface_block *) (yyvsp[0].node);
      if (block->layout.has_layout() || block->layout.has_memory()) {
         if (!block->default_layout.merge_qualifier(& (yylsp[0]), state, block->layout, false)) {
            YYERROR;
         }
      }
      block->layout = block->default_layout;
      if (!block->layout.push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }
      (yyval.node) = (yyvsp[0].node);
   }
#line 3065 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 113:
#line 859 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.function) = (yyvsp[-1].function);
      (yyval.function)->parameters.push_tail(& (yyvsp[0].parameter_declarator)->link);
   }
#line 3074 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 114:
#line 864 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.function) = (yyvsp[-2].function);
      (yyval.function)->parameters.push_tail(& (yyvsp[0].parameter_declarator)->link);
   }
#line 3083 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 115:
#line 872 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.function) = new(ctx) ast_function();
      (yyval.function)->set_location((yylsp[-1]));
      (yyval.function)->return_type = (yyvsp[-2].fully_specified_type);
      (yyval.function)->identifier = (yyvsp[-1].identifier);

      if ((yyvsp[-2].fully_specified_type)->qualifier.is_subroutine_decl()) {
         /* add type for IDENTIFIER search */
         state->symbols->add_type((yyvsp[-1].identifier), glsl_type::get_subroutine_instance((yyvsp[-1].identifier)));
      } else
         state->symbols->add_function(new(state) ir_function((yyvsp[-1].identifier)));
      state->symbols->push_scope();
   }
#line 3102 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 116:
#line 890 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.parameter_declarator) = new(ctx) ast_parameter_declarator();
      (yyval.parameter_declarator)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.parameter_declarator)->type = new(ctx) ast_fully_specified_type();
      (yyval.parameter_declarator)->type->set_location((yylsp[-1]));
      (yyval.parameter_declarator)->type->specifier = (yyvsp[-1].type_specifier);
      (yyval.parameter_declarator)->identifier = (yyvsp[0].identifier);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[0].identifier), ir_var_auto));
   }
#line 3117 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 117:
#line 901 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.parameter_declarator) = new(ctx) ast_parameter_declarator();
      (yyval.parameter_declarator)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.parameter_declarator)->type = new(ctx) ast_fully_specified_type();
      (yyval.parameter_declarator)->type->set_location((yylsp[-2]));
      (yyval.parameter_declarator)->type->specifier = (yyvsp[-2].type_specifier);
      (yyval.parameter_declarator)->identifier = (yyvsp[-1].identifier);
      (yyval.parameter_declarator)->array_specifier = (yyvsp[0].array_specifier);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-1].identifier), ir_var_auto));
   }
#line 3133 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 118:
#line 916 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.parameter_declarator) = (yyvsp[0].parameter_declarator);
      (yyval.parameter_declarator)->type->qualifier = (yyvsp[-1].type_qualifier);
      if (!(yyval.parameter_declarator)->type->qualifier.push_to_global(& (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 3145 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 119:
#line 924 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.parameter_declarator) = new(ctx) ast_parameter_declarator();
      (yyval.parameter_declarator)->set_location((yylsp[0]));
      (yyval.parameter_declarator)->type = new(ctx) ast_fully_specified_type();
      (yyval.parameter_declarator)->type->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.parameter_declarator)->type->qualifier = (yyvsp[-1].type_qualifier);
      if (!(yyval.parameter_declarator)->type->qualifier.push_to_global(& (yylsp[-1]), state)) {
         YYERROR;
      }
      (yyval.parameter_declarator)->type->specifier = (yyvsp[0].type_specifier);
   }
#line 3162 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 120:
#line 940 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
   }
#line 3170 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 121:
#line 944 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.constant)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate const qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.constant = 1;
   }
#line 3182 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 122:
#line 952 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precise qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 3194 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 123:
#line 960 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (((yyvsp[-1].type_qualifier).flags.q.in || (yyvsp[-1].type_qualifier).flags.q.out) && ((yyvsp[0].type_qualifier).flags.q.in || (yyvsp[0].type_qualifier).flags.q.out))
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate in/out/inout qualifier");

      if (!state->has_420pack_or_es31() && (yyvsp[0].type_qualifier).flags.q.constant)
         _mesa_glsl_error(&(yylsp[-1]), state, "in/out/inout must come after const "
                                      "or precise");

      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 3210 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 124:
#line 972 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).precision != ast_precision_none)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precision qualifier");

      if (!state->has_420pack_or_es31() &&
          (yyvsp[0].type_qualifier).flags.i != 0)
         _mesa_glsl_error(&(yylsp[-1]), state, "precision qualifiers must come last");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).precision = (yyvsp[-1].n);
   }
#line 3226 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 125:
#line 984 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 3235 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 126:
#line 991 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 3244 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 127:
#line 996 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 3253 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 128:
#line 1001 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 3263 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 131:
#line 1015 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = (yyvsp[-2].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[0].identifier), ir_var_auto));
   }
#line 3277 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 132:
#line 1025 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      decl->set_location_range((yylsp[-1]), (yylsp[0]));

      (yyval.declarator_list) = (yyvsp[-3].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-1].identifier), ir_var_auto));
   }
#line 3291 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 133:
#line 1035 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-3].identifier), (yyvsp[-2].array_specifier), (yyvsp[0].expression));
      decl->set_location_range((yylsp[-3]), (yylsp[-2]));

      (yyval.declarator_list) = (yyvsp[-5].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-3].identifier), ir_var_auto));
   }
#line 3305 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 134:
#line 1045 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      decl->set_location((yylsp[-2]));

      (yyval.declarator_list) = (yyvsp[-4].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-2].identifier), ir_var_auto));
   }
#line 3319 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 135:
#line 1059 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      /* Empty declaration list is valid. */
      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[0].fully_specified_type));
      (yyval.declarator_list)->set_location((yylsp[0]));
   }
#line 3330 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 136:
#line 1066 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-1].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[0].identifier), ir_var_auto));
   }
#line 3345 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 137:
#line 1077 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      decl->set_location_range((yylsp[-1]), (yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-2].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-1].identifier), ir_var_auto));
   }
#line 3360 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 138:
#line 1088 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-3].identifier), (yyvsp[-2].array_specifier), (yyvsp[0].expression));
      decl->set_location_range((yylsp[-3]), (yylsp[-2]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-4].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-4]), (yylsp[-2]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-3].identifier), ir_var_auto));
   }
#line 3375 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 139:
#line 1099 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      decl->set_location((yylsp[-2]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-3].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-3]), (yylsp[-2]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-2].identifier), ir_var_auto));
   }
#line 3390 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 140:
#line 1110 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list(NULL);
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->invariant = true;

      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 3406 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 141:
#line 1122 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list(NULL);
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->precise = true;

      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 3422 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 142:
#line 1137 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.fully_specified_type) = new(ctx) ast_fully_specified_type();
      (yyval.fully_specified_type)->set_location((yylsp[0]));
      (yyval.fully_specified_type)->specifier = (yyvsp[0].type_specifier);
   }
#line 3433 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 143:
#line 1144 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.fully_specified_type) = new(ctx) ast_fully_specified_type();
      (yyval.fully_specified_type)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.fully_specified_type)->qualifier = (yyvsp[-1].type_qualifier);
      if (!(yyval.fully_specified_type)->qualifier.push_to_global(& (yylsp[-1]), state)) {
         YYERROR;
      }
      (yyval.fully_specified_type)->specifier = (yyvsp[0].type_specifier);
      if ((yyval.fully_specified_type)->specifier->structure != NULL &&
          (yyval.fully_specified_type)->specifier->structure->is_declaration) {
            (yyval.fully_specified_type)->specifier->structure->layout = &(yyval.fully_specified_type)->qualifier;
      }
   }
#line 3452 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 144:
#line 1162 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
   }
#line 3460 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 146:
#line 1170 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-2].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[0]), state, (yyvsp[0].type_qualifier), true)) {
         YYERROR;
      }
   }
#line 3471 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 147:
#line 1180 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));

      /* Layout qualifiers for ARB_fragment_coord_conventions. */
      if (!(yyval.type_qualifier).flags.i && (state->ARB_fragment_coord_conventions_enable ||
                          state->is_version(150, 0))) {
         if (match_layout_qualifier((yyvsp[0].identifier), "origin_upper_left", state) == 0) {
            (yyval.type_qualifier).flags.q.origin_upper_left = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "pixel_center_integer",
                                           state) == 0) {
            (yyval.type_qualifier).flags.q.pixel_center_integer = 1;
         }

         if ((yyval.type_qualifier).flags.i && state->ARB_fragment_coord_conventions_warn) {
            _mesa_glsl_warning(& (yylsp[0]), state,
                               "GL_ARB_fragment_coord_conventions layout "
                               "identifier `%s' used", (yyvsp[0].identifier));
         }
      }

      /* Layout qualifiers for AMD/ARB_conservative_depth. */
      if (!(yyval.type_qualifier).flags.i &&
          (state->AMD_conservative_depth_enable ||
           state->ARB_conservative_depth_enable ||
           state->is_version(420, 0))) {
         if (match_layout_qualifier((yyvsp[0].identifier), "depth_any", state) == 0) {
            (yyval.type_qualifier).flags.q.depth_type = 1;
            (yyval.type_qualifier).depth_type = ast_depth_any;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "depth_greater", state) == 0) {
            (yyval.type_qualifier).flags.q.depth_type = 1;
            (yyval.type_qualifier).depth_type = ast_depth_greater;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "depth_less", state) == 0) {
            (yyval.type_qualifier).flags.q.depth_type = 1;
            (yyval.type_qualifier).depth_type = ast_depth_less;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "depth_unchanged",
                                           state) == 0) {
            (yyval.type_qualifier).flags.q.depth_type = 1;
            (yyval.type_qualifier).depth_type = ast_depth_unchanged;
         }

         if ((yyval.type_qualifier).flags.i && state->AMD_conservative_depth_warn) {
            _mesa_glsl_warning(& (yylsp[0]), state,
                               "GL_AMD_conservative_depth "
                               "layout qualifier `%s' is used", (yyvsp[0].identifier));
         }
         if ((yyval.type_qualifier).flags.i && state->ARB_conservative_depth_warn) {
            _mesa_glsl_warning(& (yylsp[0]), state,
                               "GL_ARB_conservative_depth "
                               "layout qualifier `%s' is used", (yyvsp[0].identifier));
         }
      }

      /* See also interface_block_layout_qualifier. */
      if (!(yyval.type_qualifier).flags.i && state->has_uniform_buffer_objects()) {
         if (match_layout_qualifier((yyvsp[0].identifier), "std140", state) == 0) {
            (yyval.type_qualifier).flags.q.std140 = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "shared", state) == 0) {
            (yyval.type_qualifier).flags.q.shared = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "std430", state) == 0) {
            (yyval.type_qualifier).flags.q.std430 = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "column_major", state) == 0) {
            (yyval.type_qualifier).flags.q.column_major = 1;
         /* "row_major" is a reserved word in GLSL 1.30+. Its token is parsed
          * below in the interface_block_layout_qualifier rule.
          *
          * It is not a reserved word in GLSL ES 3.00, so it's handled here as
          * an identifier.
          *
          * Also, this takes care of alternate capitalizations of
          * "row_major" (which is necessary because layout qualifiers
          * are case-insensitive in desktop GLSL).
          */
         } else if (match_layout_qualifier((yyvsp[0].identifier), "row_major", state) == 0) {
            (yyval.type_qualifier).flags.q.row_major = 1;
         /* "packed" is a reserved word in GLSL, and its token is
          * parsed below in the interface_block_layout_qualifier rule.
          * However, we must take care of alternate capitalizations of
          * "packed", because layout qualifiers are case-insensitive
          * in desktop GLSL.
          */
         } else if (match_layout_qualifier((yyvsp[0].identifier), "packed", state) == 0) {
           (yyval.type_qualifier).flags.q.packed = 1;
         }

         if ((yyval.type_qualifier).flags.i && state->ARB_uniform_buffer_object_warn) {
            _mesa_glsl_warning(& (yylsp[0]), state,
                               "#version 140 / GL_ARB_uniform_buffer_object "
                               "layout qualifier `%s' is used", (yyvsp[0].identifier));
         }
      }

      /* Layout qualifiers for GLSL 1.50 geometry shaders. */
      if (!(yyval.type_qualifier).flags.i) {
         static const struct {
            const char *s;
            GLenum e;
         } map[] = {
                 { "points", GL_POINTS },
                 { "lines", GL_LINES },
                 { "lines_adjacency", GL_LINES_ADJACENCY },
                 { "line_strip", GL_LINE_STRIP },
                 { "triangles", GL_TRIANGLES },
                 { "triangles_adjacency", GL_TRIANGLES_ADJACENCY },
                 { "triangle_strip", GL_TRIANGLE_STRIP },
         };
         for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
            if (match_layout_qualifier((yyvsp[0].identifier), map[i].s, state) == 0) {
               (yyval.type_qualifier).flags.q.prim_type = 1;
               (yyval.type_qualifier).prim_type = map[i].e;
               break;
            }
         }

         if ((yyval.type_qualifier).flags.i && !state->has_geometry_shader() &&
             !state->has_tessellation_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state, "#version 150 layout "
                             "qualifier `%s' used", (yyvsp[0].identifier));
         }
      }

      /* Layout qualifiers for ARB_shader_image_load_store. */
      if (state->has_shader_image_load_store()) {
         if (!(yyval.type_qualifier).flags.i) {
            static const struct {
               const char *name;
               GLenum format;
               glsl_base_type base_type;
               /** Minimum desktop GLSL version required for the image
                * format.  Use 130 if already present in the original
                * ARB extension.
                */
               unsigned required_glsl;
               /** Minimum GLSL ES version required for the image format. */
               unsigned required_essl;
               /* NV_image_formats */
               bool nv_image_formats;
            } map[] = {
               { "rgba32f", GL_RGBA32F, GLSL_TYPE_FLOAT, 130, 310, false },
               { "rgba16f", GL_RGBA16F, GLSL_TYPE_FLOAT, 130, 310, false },
               { "rg32f", GL_RG32F, GLSL_TYPE_FLOAT, 130, 0, true },
               { "rg16f", GL_RG16F, GLSL_TYPE_FLOAT, 130, 0, true },
               { "r11f_g11f_b10f", GL_R11F_G11F_B10F, GLSL_TYPE_FLOAT, 130, 0, true },
               { "r32f", GL_R32F, GLSL_TYPE_FLOAT, 130, 310, false },
               { "r16f", GL_R16F, GLSL_TYPE_FLOAT, 130, 0, true },
               { "rgba32ui", GL_RGBA32UI, GLSL_TYPE_UINT, 130, 310, false },
               { "rgba16ui", GL_RGBA16UI, GLSL_TYPE_UINT, 130, 310, false },
               { "rgb10_a2ui", GL_RGB10_A2UI, GLSL_TYPE_UINT, 130, 0, true },
               { "rgba8ui", GL_RGBA8UI, GLSL_TYPE_UINT, 130, 310, false },
               { "rg32ui", GL_RG32UI, GLSL_TYPE_UINT, 130, 0, true },
               { "rg16ui", GL_RG16UI, GLSL_TYPE_UINT, 130, 0, true },
               { "rg8ui", GL_RG8UI, GLSL_TYPE_UINT, 130, 0, true },
               { "r32ui", GL_R32UI, GLSL_TYPE_UINT, 130, 310, false },
               { "r16ui", GL_R16UI, GLSL_TYPE_UINT, 130, 0, true },
               { "r8ui", GL_R8UI, GLSL_TYPE_UINT, 130, 0, true },
               { "rgba32i", GL_RGBA32I, GLSL_TYPE_INT, 130, 310, false },
               { "rgba16i", GL_RGBA16I, GLSL_TYPE_INT, 130, 310, false },
               { "rgba8i", GL_RGBA8I, GLSL_TYPE_INT, 130, 310, false },
               { "rg32i", GL_RG32I, GLSL_TYPE_INT, 130, 0, true },
               { "rg16i", GL_RG16I, GLSL_TYPE_INT, 130, 0, true },
               { "rg8i", GL_RG8I, GLSL_TYPE_INT, 130, 0, true },
               { "r32i", GL_R32I, GLSL_TYPE_INT, 130, 310, false },
               { "r16i", GL_R16I, GLSL_TYPE_INT, 130, 0, true },
               { "r8i", GL_R8I, GLSL_TYPE_INT, 130, 0, true },
               { "rgba16", GL_RGBA16, GLSL_TYPE_FLOAT, 130, 0, false },
               { "rgb10_a2", GL_RGB10_A2, GLSL_TYPE_FLOAT, 130, 0, true },
               { "rgba8", GL_RGBA8, GLSL_TYPE_FLOAT, 130, 310, false },
               { "rg16", GL_RG16, GLSL_TYPE_FLOAT, 130, 0, false },
               { "rg8", GL_RG8, GLSL_TYPE_FLOAT, 130, 0, true },
               { "r16", GL_R16, GLSL_TYPE_FLOAT, 130, 0, false },
               { "r8", GL_R8, GLSL_TYPE_FLOAT, 130, 0, true },
               { "rgba16_snorm", GL_RGBA16_SNORM, GLSL_TYPE_FLOAT, 130, 0, false },
               { "rgba8_snorm", GL_RGBA8_SNORM, GLSL_TYPE_FLOAT, 130, 310, false },
               { "rg16_snorm", GL_RG16_SNORM, GLSL_TYPE_FLOAT, 130, 0, false },
               { "rg8_snorm", GL_RG8_SNORM, GLSL_TYPE_FLOAT, 130, 0, true },
               { "r16_snorm", GL_R16_SNORM, GLSL_TYPE_FLOAT, 130, 0, false },
               { "r8_snorm", GL_R8_SNORM, GLSL_TYPE_FLOAT, 130, 0, true }
            };

            for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
               if ((state->is_version(map[i].required_glsl,
                                      map[i].required_essl) ||
                    (state->NV_image_formats_enable &&
                     map[i].nv_image_formats)) &&
                   match_layout_qualifier((yyvsp[0].identifier), map[i].name, state) == 0) {
                  (yyval.type_qualifier).flags.q.explicit_image_format = 1;
                  (yyval.type_qualifier).image_format = map[i].format;
                  (yyval.type_qualifier).image_base_type = map[i].base_type;
                  break;
               }
            }
         }
      }

      if (!(yyval.type_qualifier).flags.i) {
         if (match_layout_qualifier((yyvsp[0].identifier), "early_fragment_tests", state) == 0) {
            /* From section 4.4.1.3 of the GLSL 4.50 specification
             * (Fragment Shader Inputs):
             *
             *  "Fragment shaders also allow the following layout
             *   qualifier on in only (not with variable declarations)
             *     layout-qualifier-id
             *        early_fragment_tests
             *   [...]"
             */
            if (state->stage != MESA_SHADER_FRAGMENT) {
               _mesa_glsl_error(& (yylsp[0]), state,
                                "early_fragment_tests layout qualifier only "
                                "valid in fragment shaders");
            }

            (yyval.type_qualifier).flags.q.early_fragment_tests = 1;
         }

         if (match_layout_qualifier((yyvsp[0].identifier), "inner_coverage", state) == 0) {
            if (state->stage != MESA_SHADER_FRAGMENT) {
               _mesa_glsl_error(& (yylsp[0]), state,
                                "inner_coverage layout qualifier only "
                                "valid in fragment shaders");
            }

	    if (state->INTEL_conservative_rasterization_enable) {
	       (yyval.type_qualifier).flags.q.inner_coverage = 1;
	    } else {
	       _mesa_glsl_error(& (yylsp[0]), state,
                                "inner_coverage layout qualifier present, "
                                "but the INTEL_conservative_rasterization extension "
                                "is not enabled.");
            }
         }

         if (match_layout_qualifier((yyvsp[0].identifier), "post_depth_coverage", state) == 0) {
            if (state->stage != MESA_SHADER_FRAGMENT) {
               _mesa_glsl_error(& (yylsp[0]), state,
                                "post_depth_coverage layout qualifier only "
                                "valid in fragment shaders");
            }

            if (state->ARB_post_depth_coverage_enable ||
		state->INTEL_conservative_rasterization_enable) {
               (yyval.type_qualifier).flags.q.post_depth_coverage = 1;
            } else {
               _mesa_glsl_error(& (yylsp[0]), state,
                                "post_depth_coverage layout qualifier present, "
                                "but the GL_ARB_post_depth_coverage extension "
                                "is not enabled.");
            }
         }

         if ((yyval.type_qualifier).flags.q.post_depth_coverage && (yyval.type_qualifier).flags.q.inner_coverage) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "post_depth_coverage & inner_coverage layout qualifiers "
                             "are mutually exclusive");
         }
      }

      /* Layout qualifiers for tessellation evaluation shaders. */
      if (!(yyval.type_qualifier).flags.i) {
         static const struct {
            const char *s;
            GLenum e;
         } map[] = {
                 /* triangles already parsed by gs-specific code */
                 { "quads", GL_QUADS },
                 { "isolines", GL_ISOLINES },
         };
         for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
            if (match_layout_qualifier((yyvsp[0].identifier), map[i].s, state) == 0) {
               (yyval.type_qualifier).flags.q.prim_type = 1;
               (yyval.type_qualifier).prim_type = map[i].e;
               break;
            }
         }

         if ((yyval.type_qualifier).flags.i && !state->has_tessellation_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "primitive mode qualifier `%s' requires "
                             "GLSL 4.00 or ARB_tessellation_shader", (yyvsp[0].identifier));
         }
      }
      if (!(yyval.type_qualifier).flags.i) {
         static const struct {
            const char *s;
            enum gl_tess_spacing e;
         } map[] = {
                 { "equal_spacing", TESS_SPACING_EQUAL },
                 { "fractional_odd_spacing", TESS_SPACING_FRACTIONAL_ODD },
                 { "fractional_even_spacing", TESS_SPACING_FRACTIONAL_EVEN },
         };
         for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
            if (match_layout_qualifier((yyvsp[0].identifier), map[i].s, state) == 0) {
               (yyval.type_qualifier).flags.q.vertex_spacing = 1;
               (yyval.type_qualifier).vertex_spacing = map[i].e;
               break;
            }
         }

         if ((yyval.type_qualifier).flags.i && !state->has_tessellation_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "vertex spacing qualifier `%s' requires "
                             "GLSL 4.00 or ARB_tessellation_shader", (yyvsp[0].identifier));
         }
      }
      if (!(yyval.type_qualifier).flags.i) {
         if (match_layout_qualifier((yyvsp[0].identifier), "cw", state) == 0) {
            (yyval.type_qualifier).flags.q.ordering = 1;
            (yyval.type_qualifier).ordering = GL_CW;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "ccw", state) == 0) {
            (yyval.type_qualifier).flags.q.ordering = 1;
            (yyval.type_qualifier).ordering = GL_CCW;
         }

         if ((yyval.type_qualifier).flags.i && !state->has_tessellation_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "ordering qualifier `%s' requires "
                             "GLSL 4.00 or ARB_tessellation_shader", (yyvsp[0].identifier));
         }
      }
      if (!(yyval.type_qualifier).flags.i) {
         if (match_layout_qualifier((yyvsp[0].identifier), "point_mode", state) == 0) {
            (yyval.type_qualifier).flags.q.point_mode = 1;
            (yyval.type_qualifier).point_mode = true;
         }

         if ((yyval.type_qualifier).flags.i && !state->has_tessellation_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "qualifier `point_mode' requires "
                             "GLSL 4.00 or ARB_tessellation_shader");
         }
      }

      if (!(yyval.type_qualifier).flags.i) {
         static const struct {
            const char *s;
            uint32_t mask;
         } map[] = {
                 { "blend_support_multiply",       BLEND_MULTIPLY },
                 { "blend_support_screen",         BLEND_SCREEN },
                 { "blend_support_overlay",        BLEND_OVERLAY },
                 { "blend_support_darken",         BLEND_DARKEN },
                 { "blend_support_lighten",        BLEND_LIGHTEN },
                 { "blend_support_colordodge",     BLEND_COLORDODGE },
                 { "blend_support_colorburn",      BLEND_COLORBURN },
                 { "blend_support_hardlight",      BLEND_HARDLIGHT },
                 { "blend_support_softlight",      BLEND_SOFTLIGHT },
                 { "blend_support_difference",     BLEND_DIFFERENCE },
                 { "blend_support_exclusion",      BLEND_EXCLUSION },
                 { "blend_support_hsl_hue",        BLEND_HSL_HUE },
                 { "blend_support_hsl_saturation", BLEND_HSL_SATURATION },
                 { "blend_support_hsl_color",      BLEND_HSL_COLOR },
                 { "blend_support_hsl_luminosity", BLEND_HSL_LUMINOSITY },
                 { "blend_support_all_equations",  BLEND_ALL },
         };
         for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
            if (match_layout_qualifier((yyvsp[0].identifier), map[i].s, state) == 0) {
               (yyval.type_qualifier).flags.q.blend_support = 1;
               state->fs_blend_support |= map[i].mask;
               break;
            }
         }

         if ((yyval.type_qualifier).flags.i &&
             !state->KHR_blend_equation_advanced_enable &&
             !state->is_version(0, 320)) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "advanced blending layout qualifiers require "
                             "ESSL 3.20 or KHR_blend_equation_advanced");
         }

         if ((yyval.type_qualifier).flags.i && state->stage != MESA_SHADER_FRAGMENT) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "advanced blending layout qualifiers only "
                             "valid in fragment shaders");
         }
      }

      /* Layout qualifiers for ARB_compute_variable_group_size. */
      if (!(yyval.type_qualifier).flags.i) {
         if (match_layout_qualifier((yyvsp[0].identifier), "local_size_variable", state) == 0) {
            (yyval.type_qualifier).flags.q.local_size_variable = 1;
         }

         if ((yyval.type_qualifier).flags.i && !state->ARB_compute_variable_group_size_enable) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "qualifier `local_size_variable` requires "
                             "ARB_compute_variable_group_size");
         }
      }

      /* Layout qualifiers for ARB_bindless_texture. */
      if (!(yyval.type_qualifier).flags.i) {
         if (match_layout_qualifier((yyvsp[0].identifier), "bindless_sampler", state) == 0)
            (yyval.type_qualifier).flags.q.bindless_sampler = 1;
         if (match_layout_qualifier((yyvsp[0].identifier), "bound_sampler", state) == 0)
            (yyval.type_qualifier).flags.q.bound_sampler = 1;

         if (state->has_shader_image_load_store()) {
            if (match_layout_qualifier((yyvsp[0].identifier), "bindless_image", state) == 0)
               (yyval.type_qualifier).flags.q.bindless_image = 1;
            if (match_layout_qualifier((yyvsp[0].identifier), "bound_image", state) == 0)
               (yyval.type_qualifier).flags.q.bound_image = 1;
         }

         if ((yyval.type_qualifier).flags.i && !state->has_bindless()) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "qualifier `%s` requires "
                             "ARB_bindless_texture", (yyvsp[0].identifier));
         }
      }

      if (!(yyval.type_qualifier).flags.i) {
         _mesa_glsl_error(& (yylsp[0]), state, "unrecognized layout identifier "
                          "`%s'", (yyvsp[0].identifier));
         YYERROR;
      }
   }
#line 3891 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 148:
#line 1596 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      void *ctx = state->linalloc;

      if ((yyvsp[0].expression)->oper != ast_int_constant &&
          (yyvsp[0].expression)->oper != ast_uint_constant &&
          !state->has_enhanced_layouts()) {
         _mesa_glsl_error(& (yylsp[-2]), state,
                          "compile-time constant expressions require "
                          "GLSL 4.40 or ARB_enhanced_layouts");
      }

      if (match_layout_qualifier("align", (yyvsp[-2].identifier), state) == 0) {
         if (!state->has_enhanced_layouts()) {
            _mesa_glsl_error(& (yylsp[-2]), state,
                             "align qualifier requires "
                             "GLSL 4.40 or ARB_enhanced_layouts");
         } else {
            (yyval.type_qualifier).flags.q.explicit_align = 1;
            (yyval.type_qualifier).align = (yyvsp[0].expression);
         }
      }

      if (match_layout_qualifier("location", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.explicit_location = 1;

         if ((yyval.type_qualifier).flags.q.attribute == 1 &&
             state->ARB_explicit_attrib_location_warn) {
            _mesa_glsl_warning(& (yylsp[-2]), state,
                               "GL_ARB_explicit_attrib_location layout "
                               "identifier `%s' used", (yyvsp[-2].identifier));
         }
         (yyval.type_qualifier).location = (yyvsp[0].expression);
      }

      if (match_layout_qualifier("component", (yyvsp[-2].identifier), state) == 0) {
         if (!state->has_enhanced_layouts()) {
            _mesa_glsl_error(& (yylsp[-2]), state,
                             "component qualifier requires "
                             "GLSL 4.40 or ARB_enhanced_layouts");
         } else {
            (yyval.type_qualifier).flags.q.explicit_component = 1;
            (yyval.type_qualifier).component = (yyvsp[0].expression);
         }
      }

      if (match_layout_qualifier("index", (yyvsp[-2].identifier), state) == 0) {
         if (state->es_shader && !state->EXT_blend_func_extended_enable) {
            _mesa_glsl_error(& (yylsp[0]), state, "index layout qualifier requires EXT_blend_func_extended");
            YYERROR;
         }

         (yyval.type_qualifier).flags.q.explicit_index = 1;
         (yyval.type_qualifier).index = (yyvsp[0].expression);
      }

      if ((state->has_420pack_or_es31() ||
           state->has_atomic_counters() ||
           state->has_shader_storage_buffer_objects()) &&
          match_layout_qualifier("binding", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.explicit_binding = 1;
         (yyval.type_qualifier).binding = (yyvsp[0].expression);
      }

      if ((state->has_atomic_counters() ||
           state->has_enhanced_layouts()) &&
          match_layout_qualifier("offset", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.explicit_offset = 1;
         (yyval.type_qualifier).offset = (yyvsp[0].expression);
      }

      if (match_layout_qualifier("max_vertices", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.max_vertices = 1;
         (yyval.type_qualifier).max_vertices = new(ctx) ast_layout_expression((yylsp[-2]), (yyvsp[0].expression));
         if (!state->has_geometry_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "#version 150 max_vertices qualifier "
                             "specified", (yyvsp[0].expression));
         }
      }

      if (state->stage == MESA_SHADER_GEOMETRY) {
         if (match_layout_qualifier("stream", (yyvsp[-2].identifier), state) == 0 &&
             state->check_explicit_attrib_stream_allowed(& (yylsp[0]))) {
            (yyval.type_qualifier).flags.q.stream = 1;
            (yyval.type_qualifier).flags.q.explicit_stream = 1;
            (yyval.type_qualifier).stream = (yyvsp[0].expression);
         }
      }

      if (state->has_enhanced_layouts()) {
         if (match_layout_qualifier("xfb_buffer", (yyvsp[-2].identifier), state) == 0) {
            (yyval.type_qualifier).flags.q.xfb_buffer = 1;
            (yyval.type_qualifier).flags.q.explicit_xfb_buffer = 1;
            (yyval.type_qualifier).xfb_buffer = (yyvsp[0].expression);
         }

         if (match_layout_qualifier("xfb_offset", (yyvsp[-2].identifier), state) == 0) {
            (yyval.type_qualifier).flags.q.explicit_xfb_offset = 1;
            (yyval.type_qualifier).offset = (yyvsp[0].expression);
         }

         if (match_layout_qualifier("xfb_stride", (yyvsp[-2].identifier), state) == 0) {
            (yyval.type_qualifier).flags.q.xfb_stride = 1;
            (yyval.type_qualifier).flags.q.explicit_xfb_stride = 1;
            (yyval.type_qualifier).xfb_stride = (yyvsp[0].expression);
         }
      }

      static const char * const local_size_qualifiers[3] = {
         "local_size_x",
         "local_size_y",
         "local_size_z",
      };
      for (int i = 0; i < 3; i++) {
         if (match_layout_qualifier(local_size_qualifiers[i], (yyvsp[-2].identifier),
                                    state) == 0) {
            if (!state->has_compute_shader()) {
               _mesa_glsl_error(& (yylsp[0]), state,
                                "%s qualifier requires GLSL 4.30 or "
                                "GLSL ES 3.10 or ARB_compute_shader",
                                local_size_qualifiers[i]);
               YYERROR;
            } else {
               (yyval.type_qualifier).flags.q.local_size |= (1 << i);
               (yyval.type_qualifier).local_size[i] = new(ctx) ast_layout_expression((yylsp[-2]), (yyvsp[0].expression));
            }
            break;
         }
      }

      if (match_layout_qualifier("invocations", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.invocations = 1;
         (yyval.type_qualifier).invocations = new(ctx) ast_layout_expression((yylsp[-2]), (yyvsp[0].expression));
         if (!state->is_version(400, 320) &&
             !state->ARB_gpu_shader5_enable &&
             !state->OES_geometry_shader_enable &&
             !state->EXT_geometry_shader_enable) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "GL_ARB_gpu_shader5 invocations "
                             "qualifier specified", (yyvsp[0].expression));
         }
      }

      /* Layout qualifiers for tessellation control shaders. */
      if (match_layout_qualifier("vertices", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.vertices = 1;
         (yyval.type_qualifier).vertices = new(ctx) ast_layout_expression((yylsp[-2]), (yyvsp[0].expression));
         if (!state->has_tessellation_shader()) {
            _mesa_glsl_error(& (yylsp[-2]), state,
                             "vertices qualifier requires GLSL 4.00 or "
                             "ARB_tessellation_shader");
         }
      }

      /* If the identifier didn't match any known layout identifiers,
       * emit an error.
       */
      if (!(yyval.type_qualifier).flags.i) {
         _mesa_glsl_error(& (yylsp[-2]), state, "unrecognized layout identifier "
                          "`%s'", (yyvsp[-2].identifier));
         YYERROR;
      }
   }
#line 4060 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 149:
#line 1761 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      /* Layout qualifiers for ARB_uniform_buffer_object. */
      if ((yyval.type_qualifier).flags.q.uniform && !state->has_uniform_buffer_objects()) {
         _mesa_glsl_error(& (yylsp[0]), state,
                          "#version 140 / GL_ARB_uniform_buffer_object "
                          "layout qualifier `%s' is used", (yyvsp[0].type_qualifier));
      } else if ((yyval.type_qualifier).flags.q.uniform && state->ARB_uniform_buffer_object_warn) {
         _mesa_glsl_warning(& (yylsp[0]), state,
                            "#version 140 / GL_ARB_uniform_buffer_object "
                            "layout qualifier `%s' is used", (yyvsp[0].type_qualifier));
      }
   }
#line 4078 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 150:
#line 1787 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.row_major = 1;
   }
#line 4087 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 151:
#line 1792 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.packed = 1;
   }
#line 4096 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 152:
#line 1797 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.shared = 1;
   }
#line 4105 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 153:
#line 1805 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.subroutine = 1;
   }
#line 4114 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 154:
#line 1810 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.subroutine = 1;
      (yyval.type_qualifier).subroutine_list = (yyvsp[-1].subroutine_list);
   }
#line 4124 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 155:
#line 1819 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
        void *ctx = state->linalloc;
        ast_declaration *decl = new(ctx)  ast_declaration((yyvsp[0].identifier), NULL, NULL);
        decl->set_location((yylsp[0]));

        (yyval.subroutine_list) = new(ctx) ast_subroutine_list();
        (yyval.subroutine_list)->declarations.push_tail(&decl->link);
   }
#line 4137 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 156:
#line 1828 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
        void *ctx = state->linalloc;
        ast_declaration *decl = new(ctx)  ast_declaration((yyvsp[0].identifier), NULL, NULL);
        decl->set_location((yylsp[0]));

        (yyval.subroutine_list) = (yyvsp[-2].subroutine_list);
        (yyval.subroutine_list)->declarations.push_tail(&decl->link);
   }
#line 4150 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 157:
#line 1840 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.smooth = 1;
   }
#line 4159 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 158:
#line 1845 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.flat = 1;
   }
#line 4168 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 159:
#line 1850 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.noperspective = 1;
   }
#line 4177 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 160:
#line 1859 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.invariant = 1;
   }
#line 4186 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 161:
#line 1864 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 4195 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 168:
#line 1875 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(&(yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).precision = (yyvsp[0].n);
   }
#line 4204 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 169:
#line 1893 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate \"precise\" qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 4216 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 170:
#line 1901 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.invariant)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate \"invariant\" qualifier");

      if (!state->has_420pack_or_es31() && (yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state,
                          "\"invariant\" must come after \"precise\"");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.invariant = 1;

      /* GLSL ES 3.00 spec, section 4.6.1 "The Invariant Qualifier":
       *
       * "Only variables output from a shader can be candidates for invariance.
       * This includes user-defined output variables and the built-in output
       * variables. As only outputs can be declared as invariant, an invariant
       * output from one shader stage will still match an input of a subsequent
       * stage without the input being declared as invariant."
       *
       * On the desktop side, this text first appears in GLSL 4.30.
       */
      if (state->is_version(430, 300) && (yyval.type_qualifier).flags.q.in)
         _mesa_glsl_error(&(yylsp[-1]), state, "invariant qualifiers cannot be used with shader inputs");
   }
#line 4245 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 171:
#line 1926 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* Section 4.3 of the GLSL 1.40 specification states:
       * "...qualified with one of these interpolation qualifiers"
       *
       * GLSL 1.30 claims to allow "one or more", but insists that:
       * "These interpolation qualifiers may only precede the qualifiers in,
       *  centroid in, out, or centroid out in a declaration."
       *
       * ...which means that e.g. smooth can't precede smooth, so there can be
       * only one after all, and the 1.40 text is a clarification, not a change.
       */
      if ((yyvsp[0].type_qualifier).has_interpolation())
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate interpolation qualifier");

      if (!state->has_420pack_or_es31() &&
          ((yyvsp[0].type_qualifier).flags.q.precise || (yyvsp[0].type_qualifier).flags.q.invariant)) {
         _mesa_glsl_error(&(yylsp[-1]), state, "interpolation qualifiers must come "
                          "after \"precise\" or \"invariant\"");
      }

      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4273 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 172:
#line 1950 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* In the absence of ARB_shading_language_420pack, layout qualifiers may
       * appear no later than auxiliary storage qualifiers. There is no
       * particularly clear spec language mandating this, but in all examples
       * the layout qualifier precedes the storage qualifier.
       *
       * We allow combinations of layout with interpolation, invariant or
       * precise qualifiers since these are useful in ARB_separate_shader_objects.
       * There is no clear spec guidance on this either.
       */
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, (yyvsp[0].type_qualifier).has_layout());
   }
#line 4291 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 173:
#line 1964 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4300 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 174:
#line 1969 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).has_auxiliary_storage()) {
         _mesa_glsl_error(&(yylsp[-1]), state,
                          "duplicate auxiliary storage qualifier (centroid or sample)");
      }

      if (!state->has_420pack_or_es31() &&
          ((yyvsp[0].type_qualifier).flags.q.precise || (yyvsp[0].type_qualifier).flags.q.invariant ||
           (yyvsp[0].type_qualifier).has_interpolation() || (yyvsp[0].type_qualifier).has_layout())) {
         _mesa_glsl_error(&(yylsp[-1]), state, "auxiliary storage qualifiers must come "
                          "just before storage qualifiers");
      }
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4320 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 175:
#line 1985 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* Section 4.3 of the GLSL 1.20 specification states:
       * "Variable declarations may have a storage qualifier specified..."
       *  1.30 clarifies this to "may have one storage qualifier".
       */
      if ((yyvsp[0].type_qualifier).has_storage())
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate storage qualifier");

      if (!state->has_420pack_or_es31() &&
          ((yyvsp[0].type_qualifier).flags.q.precise || (yyvsp[0].type_qualifier).flags.q.invariant || (yyvsp[0].type_qualifier).has_interpolation() ||
           (yyvsp[0].type_qualifier).has_layout() || (yyvsp[0].type_qualifier).has_auxiliary_storage())) {
         _mesa_glsl_error(&(yylsp[-1]), state, "storage qualifiers must come after "
                          "precise, invariant, interpolation, layout and auxiliary "
                          "storage qualifiers");
      }

      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4344 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 176:
#line 2005 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).precision != ast_precision_none)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precision qualifier");

      if (!(state->has_420pack_or_es31()) &&
          (yyvsp[0].type_qualifier).flags.i != 0)
         _mesa_glsl_error(&(yylsp[-1]), state, "precision qualifiers must come last");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).precision = (yyvsp[-1].n);
   }
#line 4360 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 177:
#line 2017 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4369 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 178:
#line 2025 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.centroid = 1;
   }
#line 4378 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 179:
#line 2030 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.sample = 1;
   }
#line 4387 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 180:
#line 2035 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.patch = 1;
   }
#line 4396 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 181:
#line 2042 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.constant = 1;
   }
#line 4405 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 182:
#line 2047 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.attribute = 1;
   }
#line 4414 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 183:
#line 2052 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.varying = 1;
   }
#line 4423 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 184:
#line 2057 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 4432 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 185:
#line 2062 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;

      if (state->stage == MESA_SHADER_GEOMETRY &&
          state->has_explicit_attrib_stream()) {
         /* Section 4.3.8.2 (Output Layout Qualifiers) of the GLSL 4.00
          * spec says:
          *
          *     "If the block or variable is declared with the stream
          *     identifier, it is associated with the specified stream;
          *     otherwise, it is associated with the current default stream."
          */
          (yyval.type_qualifier).flags.q.stream = 1;
          (yyval.type_qualifier).flags.q.explicit_stream = 0;
          (yyval.type_qualifier).stream = state->out_qualifier->stream;
      }

      if (state->has_enhanced_layouts()) {
          (yyval.type_qualifier).flags.q.xfb_buffer = 1;
          (yyval.type_qualifier).flags.q.explicit_xfb_buffer = 0;
          (yyval.type_qualifier).xfb_buffer = state->out_qualifier->xfb_buffer;
      }
   }
#line 4461 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 186:
#line 2087 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
      (yyval.type_qualifier).flags.q.out = 1;

      if (!state->has_framebuffer_fetch() ||
          !state->is_version(130, 300) ||
          state->stage != MESA_SHADER_FRAGMENT)
         _mesa_glsl_error(&(yylsp[0]), state, "A single interface variable cannot be "
                          "declared as both input and output");
   }
#line 4477 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 187:
#line 2099 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.uniform = 1;
   }
#line 4486 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 188:
#line 2104 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.buffer = 1;
   }
#line 4495 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 189:
#line 2109 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.shared_storage = 1;
   }
#line 4504 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 190:
#line 2117 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.coherent = 1;
   }
#line 4513 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 191:
#line 2122 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q._volatile = 1;
   }
#line 4522 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 192:
#line 2127 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      STATIC_ASSERT(sizeof((yyval.type_qualifier).flags.q) <= sizeof((yyval.type_qualifier).flags.i));
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.restrict_flag = 1;
   }
#line 4532 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 193:
#line 2133 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.read_only = 1;
   }
#line 4541 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 194:
#line 2138 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.write_only = 1;
   }
#line 4550 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 195:
#line 2146 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.array_specifier) = new(ctx) ast_array_specifier((yylsp[-1]), new(ctx) ast_expression(
                                                  ast_unsized_array_dim, NULL,
                                                  NULL, NULL));
      (yyval.array_specifier)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 4562 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 196:
#line 2154 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.array_specifier) = new(ctx) ast_array_specifier((yylsp[-2]), (yyvsp[-1].expression));
      (yyval.array_specifier)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 4572 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 197:
#line 2160 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.array_specifier) = (yyvsp[-2].array_specifier);

      if (state->check_arrays_of_arrays_allowed(& (yylsp[-2]))) {
         (yyval.array_specifier)->add_dimension(new(ctx) ast_expression(ast_unsized_array_dim, NULL,
                                                   NULL, NULL));
      }
   }
#line 4586 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 198:
#line 2170 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.array_specifier) = (yyvsp[-3].array_specifier);

      if (state->check_arrays_of_arrays_allowed(& (yylsp[-3]))) {
         (yyval.array_specifier)->add_dimension((yyvsp[-1].expression));
      }
   }
#line 4598 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 200:
#line 2182 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_specifier) = (yyvsp[-1].type_specifier);
      (yyval.type_specifier)->array_specifier = (yyvsp[0].array_specifier);
   }
#line 4607 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 201:
#line 2190 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].type));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 4617 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 202:
#line 2196 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].struct_specifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 4627 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 203:
#line 2202 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].identifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 4637 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 204:
#line 2210 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.type) = glsl_type::void_type; }
#line 4643 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 205:
#line 2211 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.type) = (yyvsp[0].type); }
#line 4649 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 206:
#line 2216 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_high;
   }
#line 4658 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 207:
#line 2221 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_medium;
   }
#line 4667 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 208:
#line 2226 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_low;
   }
#line 4676 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 209:
#line 2234 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.struct_specifier) = new(ctx) ast_struct_specifier((yyvsp[-3].identifier), (yyvsp[-1].declarator_list));
      (yyval.struct_specifier)->set_location_range((yylsp[-3]), (yylsp[0]));
      state->symbols->add_type((yyvsp[-3].identifier), glsl_type::void_type);
   }
#line 4687 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 210:
#line 2241 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;

      /* All anonymous structs have the same name. This simplifies matching of
       * globals whose type is an unnamed struct.
       *
       * It also avoids a memory leak when the same shader is compiled over and
       * over again.
       */
      (yyval.struct_specifier) = new(ctx) ast_struct_specifier("#anon_struct", (yyvsp[-1].declarator_list));

      (yyval.struct_specifier)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 4705 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 211:
#line 2258 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[0].declarator_list);
      (yyvsp[0].declarator_list)->link.self_link();
   }
#line 4714 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 212:
#line 2263 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[-1].declarator_list);
      (yyval.declarator_list)->link.insert_before(& (yyvsp[0].declarator_list)->link);
   }
#line 4723 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 213:
#line 2271 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_fully_specified_type *const type = (yyvsp[-2].fully_specified_type);
      type->set_location((yylsp[-2]));

      if (state->has_bindless()) {
         ast_type_qualifier input_layout_mask;

         /* Allow to declare qualifiers for images. */
         input_layout_mask.flags.i = 0;
         input_layout_mask.flags.q.coherent = 1;
         input_layout_mask.flags.q._volatile = 1;
         input_layout_mask.flags.q.restrict_flag = 1;
         input_layout_mask.flags.q.read_only = 1;
         input_layout_mask.flags.q.write_only = 1;
         input_layout_mask.flags.q.explicit_image_format = 1;

         if ((type->qualifier.flags.i & ~input_layout_mask.flags.i) != 0) {
            _mesa_glsl_error(&(yylsp[-2]), state,
                             "only precision and image qualifiers may be "
                             "applied to structure members");
         }
      } else {
         if (type->qualifier.flags.i != 0)
            _mesa_glsl_error(&(yylsp[-2]), state,
                             "only precision qualifiers may be applied to "
                             "structure members");
      }

      (yyval.declarator_list) = new(ctx) ast_declarator_list(type);
      (yyval.declarator_list)->set_location((yylsp[-1]));

      (yyval.declarator_list)->declarations.push_degenerate_list_at_head(& (yyvsp[-1].declaration)->link);
   }
#line 4762 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 214:
#line 2309 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declaration) = (yyvsp[0].declaration);
      (yyvsp[0].declaration)->link.self_link();
   }
#line 4771 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 215:
#line 2314 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declaration) = (yyvsp[-2].declaration);
      (yyval.declaration)->link.insert_before(& (yyvsp[0].declaration)->link);
   }
#line 4780 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 216:
#line 2322 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.declaration) = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      (yyval.declaration)->set_location((yylsp[0]));
   }
#line 4790 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 217:
#line 2328 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.declaration) = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      (yyval.declaration)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 4800 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 219:
#line 2338 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
   }
#line 4808 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 220:
#line 2342 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-2].expression);
   }
#line 4816 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 221:
#line 2349 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_aggregate_initializer();
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 4827 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 222:
#line 2356 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyvsp[-2].expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 4835 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 224:
#line 2368 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (ast_node *) (yyvsp[0].compound_statement); }
#line 4841 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 232:
#line 2383 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(true, NULL);
      (yyval.compound_statement)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 4851 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 233:
#line 2389 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->symbols->push_scope();
   }
#line 4859 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 234:
#line 2393 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(true, (yyvsp[-1].node));
      (yyval.compound_statement)->set_location_range((yylsp[-3]), (yylsp[0]));
      state->symbols->pop_scope();
   }
#line 4870 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 235:
#line 2402 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (ast_node *) (yyvsp[0].compound_statement); }
#line 4876 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 237:
#line 2408 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(false, NULL);
      (yyval.compound_statement)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 4886 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 238:
#line 2414 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(false, (yyvsp[-1].node));
      (yyval.compound_statement)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 4896 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 239:
#line 2423 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].node) == NULL) {
         _mesa_glsl_error(& (yylsp[0]), state, "<nil> statement");
         assert((yyvsp[0].node) != NULL);
      }

      (yyval.node) = (yyvsp[0].node);
      (yyval.node)->link.self_link();
   }
#line 4910 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 240:
#line 2433 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].node) == NULL) {
         _mesa_glsl_error(& (yylsp[0]), state, "<nil> statement");
         assert((yyvsp[0].node) != NULL);
      }
      (yyval.node) = (yyvsp[-1].node);
      (yyval.node)->link.insert_before(& (yyvsp[0].node)->link);
   }
#line 4923 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 241:
#line 2445 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_expression_statement(NULL);
      (yyval.node)->set_location((yylsp[0]));
   }
#line 4933 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 242:
#line 2451 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_expression_statement((yyvsp[-1].expression));
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 4943 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 243:
#line 2460 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = new(state->linalloc) ast_selection_statement((yyvsp[-2].expression), (yyvsp[0].selection_rest_statement).then_statement,
                                                        (yyvsp[0].selection_rest_statement).else_statement);
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 4953 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 244:
#line 2469 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.selection_rest_statement).then_statement = (yyvsp[-2].node);
      (yyval.selection_rest_statement).else_statement = (yyvsp[0].node);
   }
#line 4962 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 245:
#line 2474 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.selection_rest_statement).then_statement = (yyvsp[0].node);
      (yyval.selection_rest_statement).else_statement = NULL;
   }
#line 4971 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 246:
#line 2482 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (ast_node *) (yyvsp[0].expression);
   }
#line 4979 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 247:
#line 2486 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      ast_declarator_list *declarator = new(ctx) ast_declarator_list((yyvsp[-3].fully_specified_type));
      decl->set_location_range((yylsp[-2]), (yylsp[0]));
      declarator->set_location((yylsp[-3]));

      declarator->declarations.push_tail(&decl->link);
      (yyval.node) = declarator;
   }
#line 4994 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 248:
#line 2504 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = new(state->linalloc) ast_switch_statement((yyvsp[-2].expression), (yyvsp[0].switch_body));
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 5003 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 249:
#line 2512 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.switch_body) = new(state->linalloc) ast_switch_body(NULL);
      (yyval.switch_body)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 5012 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 250:
#line 2517 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.switch_body) = new(state->linalloc) ast_switch_body((yyvsp[-1].case_statement_list));
      (yyval.switch_body)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 5021 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 251:
#line 2525 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label) = new(state->linalloc) ast_case_label((yyvsp[-1].expression));
      (yyval.case_label)->set_location((yylsp[-1]));
   }
#line 5030 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 252:
#line 2530 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label) = new(state->linalloc) ast_case_label(NULL);
      (yyval.case_label)->set_location((yylsp[0]));
   }
#line 5039 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 253:
#line 2538 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_label_list *labels = new(state->linalloc) ast_case_label_list();

      labels->labels.push_tail(& (yyvsp[0].case_label)->link);
      (yyval.case_label_list) = labels;
      (yyval.case_label_list)->set_location((yylsp[0]));
   }
#line 5051 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 254:
#line 2546 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label_list) = (yyvsp[-1].case_label_list);
      (yyval.case_label_list)->labels.push_tail(& (yyvsp[0].case_label)->link);
   }
#line 5060 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 255:
#line 2554 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_statement *stmts = new(state->linalloc) ast_case_statement((yyvsp[-1].case_label_list));
      stmts->set_location((yylsp[0]));

      stmts->stmts.push_tail(& (yyvsp[0].node)->link);
      (yyval.case_statement) = stmts;
   }
#line 5072 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 256:
#line 2562 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_statement) = (yyvsp[-1].case_statement);
      (yyval.case_statement)->stmts.push_tail(& (yyvsp[0].node)->link);
   }
#line 5081 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 257:
#line 2570 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_statement_list *cases= new(state->linalloc) ast_case_statement_list();
      cases->set_location((yylsp[0]));

      cases->cases.push_tail(& (yyvsp[0].case_statement)->link);
      (yyval.case_statement_list) = cases;
   }
#line 5093 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 258:
#line 2578 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_statement_list) = (yyvsp[-1].case_statement_list);
      (yyval.case_statement_list)->cases.push_tail(& (yyvsp[0].case_statement)->link);
   }
#line 5102 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 259:
#line 2586 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_while,
                                            NULL, (yyvsp[-2].node), NULL, (yyvsp[0].node));
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[-1]));
   }
#line 5113 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 260:
#line 2593 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_do_while,
                                            NULL, (yyvsp[-2].expression), NULL, (yyvsp[-5].node));
      (yyval.node)->set_location_range((yylsp[-6]), (yylsp[-1]));
   }
#line 5124 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 261:
#line 2600 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_for,
                                            (yyvsp[-3].node), (yyvsp[-2].for_rest_statement).cond, (yyvsp[-2].for_rest_statement).rest, (yyvsp[0].node));
      (yyval.node)->set_location_range((yylsp[-5]), (yylsp[0]));
   }
#line 5135 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 265:
#line 2616 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
   }
#line 5143 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 266:
#line 2623 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.for_rest_statement).cond = (yyvsp[-1].node);
      (yyval.for_rest_statement).rest = NULL;
   }
#line 5152 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 267:
#line 2628 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.for_rest_statement).cond = (yyvsp[-2].node);
      (yyval.for_rest_statement).rest = (yyvsp[0].expression);
   }
#line 5161 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 268:
#line 2637 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_continue, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 5171 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 269:
#line 2643 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_break, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 5181 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 270:
#line 2649 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_return, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 5191 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 271:
#line 2655 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_return, (yyvsp[-1].expression));
      (yyval.node)->set_location_range((yylsp[-2]), (yylsp[-1]));
   }
#line 5201 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 272:
#line 2661 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_discard, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 5211 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 273:
#line 2669 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].function_definition); }
#line 5217 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 274:
#line 2670 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].node); }
#line 5223 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 275:
#line 2671 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = NULL; }
#line 5229 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 276:
#line 2672 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].node); }
#line 5235 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 277:
#line 2677 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.function_definition) = new(ctx) ast_function_definition();
      (yyval.function_definition)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.function_definition)->prototype = (yyvsp[-1].function);
      (yyval.function_definition)->body = (yyvsp[0].compound_statement);

      state->symbols->pop_scope();
   }
#line 5249 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 278:
#line 2691 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[0].interface_block);
   }
#line 5257 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 279:
#line 2695 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *block = (ast_interface_block *) (yyvsp[0].node);

      if (!(yyvsp[-1].type_qualifier).merge_qualifier(& (yylsp[-1]), state, block->layout, false,
                              block->layout.has_layout())) {
         YYERROR;
      }

      block->layout = (yyvsp[-1].type_qualifier);

      (yyval.node) = block;
   }
#line 5274 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 280:
#line 2708 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *block = (ast_interface_block *)(yyvsp[0].node);

      if (!block->default_layout.flags.q.buffer) {
            _mesa_glsl_error(& (yylsp[-1]), state,
                             "memory qualifiers can only be used in the "
                             "declaration of shader storage blocks");
      }
      if (!(yyvsp[-1].type_qualifier).merge_qualifier(& (yylsp[-1]), state, block->layout, false)) {
         YYERROR;
      }
      block->layout = (yyvsp[-1].type_qualifier);
      (yyval.node) = block;
   }
#line 5293 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 281:
#line 2726 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *const block = (yyvsp[-1].interface_block);

      if ((yyvsp[-6].type_qualifier).flags.q.uniform) {
         block->default_layout = *state->default_uniform_qualifier;
      } else if ((yyvsp[-6].type_qualifier).flags.q.buffer) {
         block->default_layout = *state->default_shader_storage_qualifier;
      }
      block->block_name = (yyvsp[-5].identifier);
      block->declarations.push_degenerate_list_at_head(& (yyvsp[-3].declarator_list)->link);

      _mesa_ast_process_interface_block(& (yylsp[-6]), state, block, (yyvsp[-6].type_qualifier));

      (yyval.interface_block) = block;
   }
#line 5313 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 282:
#line 2745 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 5322 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 283:
#line 2750 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 5331 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 284:
#line 2755 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.uniform = 1;
   }
#line 5340 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 285:
#line 2760 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.buffer = 1;
   }
#line 5349 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 286:
#line 2765 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!(yyvsp[-1].type_qualifier).flags.q.patch) {
         _mesa_glsl_error(&(yylsp[-1]), state, "invalid interface qualifier");
      }
      if ((yyvsp[0].type_qualifier).has_auxiliary_storage()) {
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate patch qualifier");
      }
      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.patch = 1;
   }
#line 5364 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 287:
#line 2779 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state->linalloc) ast_interface_block(NULL, NULL);
   }
#line 5372 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 288:
#line 2783 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state->linalloc) ast_interface_block((yyvsp[0].identifier), NULL);
      (yyval.interface_block)->set_location((yylsp[0]));
   }
#line 5381 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 289:
#line 2788 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state->linalloc) ast_interface_block((yyvsp[-1].identifier), (yyvsp[0].array_specifier));
      (yyval.interface_block)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 5390 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 290:
#line 2796 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[0].declarator_list);
      (yyvsp[0].declarator_list)->link.self_link();
   }
#line 5399 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 291:
#line 2801 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[-1].declarator_list);
      (yyvsp[0].declarator_list)->link.insert_before(& (yyval.declarator_list)->link);
   }
#line 5408 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 292:
#line 2809 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_fully_specified_type *type = (yyvsp[-2].fully_specified_type);
      type->set_location((yylsp[-2]));

      if (type->qualifier.flags.q.attribute) {
         _mesa_glsl_error(& (yylsp[-2]), state,
                          "keyword 'attribute' cannot be used with "
                          "interface block member");
      } else if (type->qualifier.flags.q.varying) {
         _mesa_glsl_error(& (yylsp[-2]), state,
                          "keyword 'varying' cannot be used with "
                          "interface block member");
      }

      (yyval.declarator_list) = new(ctx) ast_declarator_list(type);
      (yyval.declarator_list)->set_location((yylsp[-1]));

      (yyval.declarator_list)->declarations.push_degenerate_list_at_head(& (yyvsp[-1].declaration)->link);
   }
#line 5433 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 293:
#line 2833 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
   }
#line 5444 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 295:
#line 2844 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
   }
#line 5455 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 297:
#line 2855 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
      if (!(yyval.type_qualifier).validate_in_qualifier(& (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 5469 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 298:
#line 2865 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!(yyvsp[-2].type_qualifier).validate_in_qualifier(& (yylsp[-2]), state)) {
         YYERROR;
      }
   }
#line 5479 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 299:
#line 2874 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
      if (!(yyval.type_qualifier).validate_out_qualifier(& (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 5493 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 300:
#line 2884 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!(yyvsp[-2].type_qualifier).validate_out_qualifier(& (yylsp[-2]), state)) {
         YYERROR;
      }
   }
#line 5503 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 301:
#line 2893 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->default_uniform_qualifier->
             merge_qualifier(& (yylsp[0]), state, (yyvsp[0].type_qualifier), false)) {
         YYERROR;
      }
      if (!state->default_uniform_qualifier->
             push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }
   }
#line 5519 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 302:
#line 2905 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->default_shader_storage_qualifier->
             merge_qualifier(& (yylsp[0]), state, (yyvsp[0].type_qualifier), false)) {
         YYERROR;
      }
      if (!state->default_shader_storage_qualifier->
             push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }

      /* From the GLSL 4.50 spec, section 4.4.5:
       *
       *     "It is a compile-time error to specify the binding identifier for
       *     the global scope or for block member declarations."
       */
      if (state->default_shader_storage_qualifier->flags.q.explicit_binding) {
         _mesa_glsl_error(& (yylsp[0]), state,
                          "binding qualifier cannot be set for default layout");
      }
   }
#line 5545 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 303:
#line 2927 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!(yyvsp[0].type_qualifier).merge_into_in_qualifier(& (yylsp[0]), state, (yyval.node))) {
         YYERROR;
      }
      if (!state->in_qualifier->push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }
   }
#line 5559 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 304:
#line 2937 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!(yyvsp[0].type_qualifier).merge_into_out_qualifier(& (yylsp[0]), state, (yyval.node))) {
         YYERROR;
      }
      if (!state->out_qualifier->push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }
   }
#line 5573 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;


#line 5577 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
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
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

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
      yyerror (&yylloc, state, YY_("syntax error"));
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
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
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
        yyerror (&yylloc, state, yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }

  yyerror_range[1] = yylloc;

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
                      yytoken, &yylval, &yylloc, state);
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

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  yyerror_range[1] = yylsp[1-yylen];
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

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp, yylsp, state);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the lookahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, yyerror_range, 2);
  *++yylsp = yyloc;

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
  yyerror (&yylloc, state, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc, state);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[*yyssp], yyvsp, yylsp, state);
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
