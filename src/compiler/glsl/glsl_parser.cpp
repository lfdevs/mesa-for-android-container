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
    BOOL_TOK = 260,
    FLOAT_TOK = 261,
    INT_TOK = 262,
    UINT_TOK = 263,
    DOUBLE_TOK = 264,
    BREAK = 265,
    BUFFER = 266,
    CONTINUE = 267,
    DO = 268,
    ELSE = 269,
    FOR = 270,
    IF = 271,
    DISCARD = 272,
    RETURN = 273,
    SWITCH = 274,
    CASE = 275,
    DEFAULT = 276,
    BVEC2 = 277,
    BVEC3 = 278,
    BVEC4 = 279,
    IVEC2 = 280,
    IVEC3 = 281,
    IVEC4 = 282,
    UVEC2 = 283,
    UVEC3 = 284,
    UVEC4 = 285,
    VEC2 = 286,
    VEC3 = 287,
    VEC4 = 288,
    DVEC2 = 289,
    DVEC3 = 290,
    DVEC4 = 291,
    INT64_TOK = 292,
    UINT64_TOK = 293,
    I64VEC2 = 294,
    I64VEC3 = 295,
    I64VEC4 = 296,
    U64VEC2 = 297,
    U64VEC3 = 298,
    U64VEC4 = 299,
    CENTROID = 300,
    IN_TOK = 301,
    OUT_TOK = 302,
    INOUT_TOK = 303,
    UNIFORM = 304,
    VARYING = 305,
    SAMPLE = 306,
    NOPERSPECTIVE = 307,
    FLAT = 308,
    SMOOTH = 309,
    MAT2X2 = 310,
    MAT2X3 = 311,
    MAT2X4 = 312,
    MAT3X2 = 313,
    MAT3X3 = 314,
    MAT3X4 = 315,
    MAT4X2 = 316,
    MAT4X3 = 317,
    MAT4X4 = 318,
    DMAT2X2 = 319,
    DMAT2X3 = 320,
    DMAT2X4 = 321,
    DMAT3X2 = 322,
    DMAT3X3 = 323,
    DMAT3X4 = 324,
    DMAT4X2 = 325,
    DMAT4X3 = 326,
    DMAT4X4 = 327,
    SAMPLER1D = 328,
    SAMPLER2D = 329,
    SAMPLER3D = 330,
    SAMPLERCUBE = 331,
    SAMPLER1DSHADOW = 332,
    SAMPLER2DSHADOW = 333,
    SAMPLERCUBESHADOW = 334,
    SAMPLER1DARRAY = 335,
    SAMPLER2DARRAY = 336,
    SAMPLER1DARRAYSHADOW = 337,
    SAMPLER2DARRAYSHADOW = 338,
    SAMPLERCUBEARRAY = 339,
    SAMPLERCUBEARRAYSHADOW = 340,
    ISAMPLER1D = 341,
    ISAMPLER2D = 342,
    ISAMPLER3D = 343,
    ISAMPLERCUBE = 344,
    ISAMPLER1DARRAY = 345,
    ISAMPLER2DARRAY = 346,
    ISAMPLERCUBEARRAY = 347,
    USAMPLER1D = 348,
    USAMPLER2D = 349,
    USAMPLER3D = 350,
    USAMPLERCUBE = 351,
    USAMPLER1DARRAY = 352,
    USAMPLER2DARRAY = 353,
    USAMPLERCUBEARRAY = 354,
    SAMPLER2DRECT = 355,
    ISAMPLER2DRECT = 356,
    USAMPLER2DRECT = 357,
    SAMPLER2DRECTSHADOW = 358,
    SAMPLERBUFFER = 359,
    ISAMPLERBUFFER = 360,
    USAMPLERBUFFER = 361,
    SAMPLER2DMS = 362,
    ISAMPLER2DMS = 363,
    USAMPLER2DMS = 364,
    SAMPLER2DMSARRAY = 365,
    ISAMPLER2DMSARRAY = 366,
    USAMPLER2DMSARRAY = 367,
    SAMPLEREXTERNALOES = 368,
    IMAGE1D = 369,
    IMAGE2D = 370,
    IMAGE3D = 371,
    IMAGE2DRECT = 372,
    IMAGECUBE = 373,
    IMAGEBUFFER = 374,
    IMAGE1DARRAY = 375,
    IMAGE2DARRAY = 376,
    IMAGECUBEARRAY = 377,
    IMAGE2DMS = 378,
    IMAGE2DMSARRAY = 379,
    IIMAGE1D = 380,
    IIMAGE2D = 381,
    IIMAGE3D = 382,
    IIMAGE2DRECT = 383,
    IIMAGECUBE = 384,
    IIMAGEBUFFER = 385,
    IIMAGE1DARRAY = 386,
    IIMAGE2DARRAY = 387,
    IIMAGECUBEARRAY = 388,
    IIMAGE2DMS = 389,
    IIMAGE2DMSARRAY = 390,
    UIMAGE1D = 391,
    UIMAGE2D = 392,
    UIMAGE3D = 393,
    UIMAGE2DRECT = 394,
    UIMAGECUBE = 395,
    UIMAGEBUFFER = 396,
    UIMAGE1DARRAY = 397,
    UIMAGE2DARRAY = 398,
    UIMAGECUBEARRAY = 399,
    UIMAGE2DMS = 400,
    UIMAGE2DMSARRAY = 401,
    IMAGE1DSHADOW = 402,
    IMAGE2DSHADOW = 403,
    IMAGE1DARRAYSHADOW = 404,
    IMAGE2DARRAYSHADOW = 405,
    COHERENT = 406,
    VOLATILE = 407,
    RESTRICT = 408,
    READONLY = 409,
    WRITEONLY = 410,
    ATOMIC_UINT = 411,
    SHARED = 412,
    STRUCT = 413,
    VOID_TOK = 414,
    WHILE = 415,
    IDENTIFIER = 416,
    TYPE_IDENTIFIER = 417,
    NEW_IDENTIFIER = 418,
    FLOATCONSTANT = 419,
    DOUBLECONSTANT = 420,
    INTCONSTANT = 421,
    UINTCONSTANT = 422,
    BOOLCONSTANT = 423,
    INT64CONSTANT = 424,
    UINT64CONSTANT = 425,
    FIELD_SELECTION = 426,
    LEFT_OP = 427,
    RIGHT_OP = 428,
    INC_OP = 429,
    DEC_OP = 430,
    LE_OP = 431,
    GE_OP = 432,
    EQ_OP = 433,
    NE_OP = 434,
    AND_OP = 435,
    OR_OP = 436,
    XOR_OP = 437,
    MUL_ASSIGN = 438,
    DIV_ASSIGN = 439,
    ADD_ASSIGN = 440,
    MOD_ASSIGN = 441,
    LEFT_ASSIGN = 442,
    RIGHT_ASSIGN = 443,
    AND_ASSIGN = 444,
    XOR_ASSIGN = 445,
    OR_ASSIGN = 446,
    SUB_ASSIGN = 447,
    INVARIANT = 448,
    PRECISE = 449,
    LOWP = 450,
    MEDIUMP = 451,
    HIGHP = 452,
    SUPERP = 453,
    PRECISION = 454,
    VERSION_TOK = 455,
    EXTENSION = 456,
    LINE = 457,
    COLON = 458,
    EOL = 459,
    INTERFACE = 460,
    OUTPUT = 461,
    PRAGMA_DEBUG_ON = 462,
    PRAGMA_DEBUG_OFF = 463,
    PRAGMA_OPTIMIZE_ON = 464,
    PRAGMA_OPTIMIZE_OFF = 465,
    PRAGMA_INVARIANT_ALL = 466,
    LAYOUT_TOK = 467,
    DOT_TOK = 468,
    ASM = 469,
    CLASS = 470,
    UNION = 471,
    ENUM = 472,
    TYPEDEF = 473,
    TEMPLATE = 474,
    THIS = 475,
    PACKED_TOK = 476,
    GOTO = 477,
    INLINE_TOK = 478,
    NOINLINE = 479,
    PUBLIC_TOK = 480,
    STATIC = 481,
    EXTERN = 482,
    EXTERNAL = 483,
    LONG_TOK = 484,
    SHORT_TOK = 485,
    HALF = 486,
    FIXED_TOK = 487,
    UNSIGNED = 488,
    INPUT_TOK = 489,
    HVEC2 = 490,
    HVEC3 = 491,
    HVEC4 = 492,
    FVEC2 = 493,
    FVEC3 = 494,
    FVEC4 = 495,
    SAMPLER3DRECT = 496,
    SIZEOF = 497,
    CAST = 498,
    NAMESPACE = 499,
    USING = 500,
    RESOURCE = 501,
    PATCH = 502,
    SUBROUTINE = 503,
    ERROR_TOK = 504,
    COMMON = 505,
    PARTITION = 506,
    ACTIVE = 507,
    FILTER = 508,
    ROW_MAJOR = 509,
    THEN = 510
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

#line 487 "glsl/glsl_parser.cpp" /* yacc.c:355  */
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

#line 517 "glsl/glsl_parser.cpp" /* yacc.c:358  */

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
#define YYLAST   5966

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  279
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  110
/* YYNRULES -- Number of rules.  */
#define YYNRULES  424
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  581

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   510

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint16 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   263,     2,     2,     2,   267,   270,     2,
     256,   257,   265,   261,   260,   262,     2,   266,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   274,   276,
     268,   275,   269,   273,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   258,     2,   259,   271,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   277,   272,   278,   264,     2,     2,     2,
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
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,   206,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   313,   313,   312,   336,   338,   345,   355,   356,   357,
     358,   359,   383,   385,   389,   390,   391,   395,   404,   412,
     420,   431,   432,   436,   443,   450,   457,   464,   471,   478,
     485,   492,   499,   500,   506,   510,   517,   523,   532,   536,
     540,   544,   545,   549,   550,   554,   560,   572,   576,   582,
     596,   597,   603,   609,   619,   620,   621,   622,   626,   627,
     633,   639,   648,   649,   655,   664,   665,   671,   680,   681,
     687,   693,   699,   708,   709,   715,   724,   725,   734,   735,
     744,   745,   754,   755,   764,   765,   774,   775,   784,   785,
     794,   795,   804,   805,   806,   807,   808,   809,   810,   811,
     812,   813,   814,   818,   822,   838,   842,   847,   851,   856,
     873,   877,   878,   882,   887,   895,   913,   924,   939,   947,
     964,   967,   975,   983,   995,  1007,  1014,  1019,  1024,  1033,
    1037,  1038,  1048,  1058,  1068,  1082,  1089,  1100,  1111,  1122,
    1133,  1145,  1160,  1167,  1185,  1192,  1193,  1203,  1619,  1784,
    1810,  1815,  1820,  1828,  1833,  1842,  1851,  1863,  1868,  1873,
    1882,  1887,  1892,  1893,  1894,  1895,  1896,  1897,  1898,  1916,
    1924,  1949,  1973,  1987,  1992,  2008,  2028,  2040,  2048,  2053,
    2058,  2065,  2070,  2075,  2080,  2085,  2110,  2122,  2127,  2132,
    2140,  2145,  2150,  2156,  2161,  2169,  2177,  2183,  2193,  2204,
    2205,  2213,  2219,  2225,  2234,  2235,  2236,  2237,  2238,  2239,
    2240,  2241,  2242,  2243,  2244,  2245,  2246,  2247,  2248,  2249,
    2250,  2251,  2252,  2253,  2254,  2255,  2256,  2257,  2258,  2259,
    2260,  2261,  2262,  2263,  2264,  2265,  2266,  2267,  2268,  2269,
    2270,  2271,  2272,  2273,  2274,  2275,  2276,  2277,  2278,  2279,
    2280,  2281,  2282,  2283,  2284,  2285,  2286,  2287,  2288,  2289,
    2290,  2291,  2292,  2293,  2294,  2295,  2296,  2297,  2298,  2299,
    2300,  2301,  2302,  2303,  2304,  2305,  2306,  2307,  2308,  2309,
    2310,  2311,  2312,  2313,  2314,  2315,  2316,  2317,  2318,  2319,
    2320,  2321,  2322,  2323,  2324,  2325,  2326,  2327,  2328,  2329,
    2330,  2331,  2332,  2333,  2334,  2335,  2336,  2337,  2338,  2339,
    2340,  2341,  2342,  2343,  2344,  2345,  2346,  2347,  2348,  2349,
    2350,  2351,  2352,  2353,  2354,  2355,  2359,  2364,  2369,  2377,
    2384,  2393,  2398,  2406,  2444,  2449,  2457,  2463,  2472,  2473,
    2477,  2484,  2491,  2498,  2504,  2505,  2509,  2510,  2511,  2512,
    2513,  2514,  2518,  2525,  2524,  2538,  2539,  2543,  2549,  2558,
    2568,  2580,  2586,  2595,  2604,  2609,  2617,  2621,  2639,  2647,
    2652,  2660,  2665,  2673,  2681,  2689,  2697,  2705,  2713,  2721,
    2728,  2735,  2745,  2746,  2750,  2752,  2758,  2763,  2772,  2778,
    2784,  2790,  2796,  2805,  2806,  2807,  2808,  2812,  2826,  2830,
    2843,  2861,  2880,  2885,  2890,  2895,  2900,  2915,  2918,  2923,
    2931,  2936,  2944,  2968,  2975,  2979,  2986,  2990,  3000,  3009,
    3019,  3028,  3040,  3062,  3072
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ATTRIBUTE", "CONST_TOK", "BOOL_TOK",
  "FLOAT_TOK", "INT_TOK", "UINT_TOK", "DOUBLE_TOK", "BREAK", "BUFFER",
  "CONTINUE", "DO", "ELSE", "FOR", "IF", "DISCARD", "RETURN", "SWITCH",
  "CASE", "DEFAULT", "BVEC2", "BVEC3", "BVEC4", "IVEC2", "IVEC3", "IVEC4",
  "UVEC2", "UVEC3", "UVEC4", "VEC2", "VEC3", "VEC4", "DVEC2", "DVEC3",
  "DVEC4", "INT64_TOK", "UINT64_TOK", "I64VEC2", "I64VEC3", "I64VEC4",
  "U64VEC2", "U64VEC3", "U64VEC4", "CENTROID", "IN_TOK", "OUT_TOK",
  "INOUT_TOK", "UNIFORM", "VARYING", "SAMPLE", "NOPERSPECTIVE", "FLAT",
  "SMOOTH", "MAT2X2", "MAT2X3", "MAT2X4", "MAT3X2", "MAT3X3", "MAT3X4",
  "MAT4X2", "MAT4X3", "MAT4X4", "DMAT2X2", "DMAT2X3", "DMAT2X4", "DMAT3X2",
  "DMAT3X3", "DMAT3X4", "DMAT4X2", "DMAT4X3", "DMAT4X4", "SAMPLER1D",
  "SAMPLER2D", "SAMPLER3D", "SAMPLERCUBE", "SAMPLER1DSHADOW",
  "SAMPLER2DSHADOW", "SAMPLERCUBESHADOW", "SAMPLER1DARRAY",
  "SAMPLER2DARRAY", "SAMPLER1DARRAYSHADOW", "SAMPLER2DARRAYSHADOW",
  "SAMPLERCUBEARRAY", "SAMPLERCUBEARRAYSHADOW", "ISAMPLER1D", "ISAMPLER2D",
  "ISAMPLER3D", "ISAMPLERCUBE", "ISAMPLER1DARRAY", "ISAMPLER2DARRAY",
  "ISAMPLERCUBEARRAY", "USAMPLER1D", "USAMPLER2D", "USAMPLER3D",
  "USAMPLERCUBE", "USAMPLER1DARRAY", "USAMPLER2DARRAY",
  "USAMPLERCUBEARRAY", "SAMPLER2DRECT", "ISAMPLER2DRECT", "USAMPLER2DRECT",
  "SAMPLER2DRECTSHADOW", "SAMPLERBUFFER", "ISAMPLERBUFFER",
  "USAMPLERBUFFER", "SAMPLER2DMS", "ISAMPLER2DMS", "USAMPLER2DMS",
  "SAMPLER2DMSARRAY", "ISAMPLER2DMSARRAY", "USAMPLER2DMSARRAY",
  "SAMPLEREXTERNALOES", "IMAGE1D", "IMAGE2D", "IMAGE3D", "IMAGE2DRECT",
  "IMAGECUBE", "IMAGEBUFFER", "IMAGE1DARRAY", "IMAGE2DARRAY",
  "IMAGECUBEARRAY", "IMAGE2DMS", "IMAGE2DMSARRAY", "IIMAGE1D", "IIMAGE2D",
  "IIMAGE3D", "IIMAGE2DRECT", "IIMAGECUBE", "IIMAGEBUFFER",
  "IIMAGE1DARRAY", "IIMAGE2DARRAY", "IIMAGECUBEARRAY", "IIMAGE2DMS",
  "IIMAGE2DMSARRAY", "UIMAGE1D", "UIMAGE2D", "UIMAGE3D", "UIMAGE2DRECT",
  "UIMAGECUBE", "UIMAGEBUFFER", "UIMAGE1DARRAY", "UIMAGE2DARRAY",
  "UIMAGECUBEARRAY", "UIMAGE2DMS", "UIMAGE2DMSARRAY", "IMAGE1DSHADOW",
  "IMAGE2DSHADOW", "IMAGE1DARRAYSHADOW", "IMAGE2DARRAYSHADOW", "COHERENT",
  "VOLATILE", "RESTRICT", "READONLY", "WRITEONLY", "ATOMIC_UINT", "SHARED",
  "STRUCT", "VOID_TOK", "WHILE", "IDENTIFIER", "TYPE_IDENTIFIER",
  "NEW_IDENTIFIER", "FLOATCONSTANT", "DOUBLECONSTANT", "INTCONSTANT",
  "UINTCONSTANT", "BOOLCONSTANT", "INT64CONSTANT", "UINT64CONSTANT",
  "FIELD_SELECTION", "LEFT_OP", "RIGHT_OP", "INC_OP", "DEC_OP", "LE_OP",
  "GE_OP", "EQ_OP", "NE_OP", "AND_OP", "OR_OP", "XOR_OP", "MUL_ASSIGN",
  "DIV_ASSIGN", "ADD_ASSIGN", "MOD_ASSIGN", "LEFT_ASSIGN", "RIGHT_ASSIGN",
  "AND_ASSIGN", "XOR_ASSIGN", "OR_ASSIGN", "SUB_ASSIGN", "INVARIANT",
  "PRECISE", "LOWP", "MEDIUMP", "HIGHP", "SUPERP", "PRECISION",
  "VERSION_TOK", "EXTENSION", "LINE", "COLON", "EOL", "INTERFACE",
  "OUTPUT", "PRAGMA_DEBUG_ON", "PRAGMA_DEBUG_OFF", "PRAGMA_OPTIMIZE_ON",
  "PRAGMA_OPTIMIZE_OFF", "PRAGMA_INVARIANT_ALL", "LAYOUT_TOK", "DOT_TOK",
  "ASM", "CLASS", "UNION", "ENUM", "TYPEDEF", "TEMPLATE", "THIS",
  "PACKED_TOK", "GOTO", "INLINE_TOK", "NOINLINE", "PUBLIC_TOK", "STATIC",
  "EXTERN", "EXTERNAL", "LONG_TOK", "SHORT_TOK", "HALF", "FIXED_TOK",
  "UNSIGNED", "INPUT_TOK", "HVEC2", "HVEC3", "HVEC4", "FVEC2", "FVEC3",
  "FVEC4", "SAMPLER3DRECT", "SIZEOF", "CAST", "NAMESPACE", "USING",
  "RESOURCE", "PATCH", "SUBROUTINE", "ERROR_TOK", "COMMON", "PARTITION",
  "ACTIVE", "FILTER", "ROW_MAJOR", "THEN", "'('", "')'", "'['", "']'",
  "','", "'+'", "'-'", "'!'", "'~'", "'*'", "'/'", "'%'", "'<'", "'>'",
  "'&'", "'^'", "'|'", "'?'", "':'", "'='", "';'", "'{'", "'}'", "$accept",
  "translation_unit", "$@1", "version_statement", "pragma_statement",
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
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   403,   404,
     405,   406,   407,   408,   409,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,   426,   427,   428,   429,   430,   431,   432,   433,   434,
     435,   436,   437,   438,   439,   440,   441,   442,   443,   444,
     445,   446,   447,   448,   449,   450,   451,   452,   453,   454,
     455,   456,   457,   458,   459,   460,   461,   462,   463,   464,
     465,   466,   467,   468,   469,   470,   471,   472,   473,   474,
     475,   476,   477,   478,   479,   480,   481,   482,   483,   484,
     485,   486,   487,   488,   489,   490,   491,   492,   493,   494,
     495,   496,   497,   498,   499,   500,   501,   502,   503,   504,
     505,   506,   507,   508,   509,   510,    40,    41,    91,    93,
      44,    43,    45,    33,   126,    42,    47,    37,    60,    62,
      38,    94,   124,    63,    58,    61,    59,   123,   125
};
# endif

#define YYPACT_NINF -486

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-486)))

#define YYTABLE_NINF -406

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -163,  -131,    59,  -486,  -132,  -486,  -140,  -486,  -486,  -486,
    -486,  -112,    85,  5101,  -486,  -486,  -137,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,   -64,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,   -46,
     -44,  -486,   -39,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -138,  -486,  -486,
    5485,  5485,  -486,  -486,  -486,    61,  -108,   -56,   -50,   -47,
     -36,   -86,  -486,   -80,  -486,  4874,  -486,   -89,  -114,   -82,
     155,  -126,  -486,   111,  5498,  5508,  5508,  5804,  5570,  5508,
    5570,  -486,   -68,  -486,  5508,  -486,  -486,  -486,  -486,  -486,
      35,  -486,  -486,  -486,  -486,  -486,    85,  5328,   -71,  -486,
    -486,  -486,  -486,  -486,  -486,  5508,  5508,  -486,  5508,  -486,
    5508,  5508,  -486,  -486,  5804,  -486,  -486,  -486,  -486,  -486,
    -119,    85,  -486,  -486,  -486,   568,  -486,  -486,   267,   267,
    -486,  -486,  -486,   267,  -486,  5804,   267,   267,   267,    85,
    -486,   -48,   -33,  -219,   -31,  -154,  -150,  -144,  -136,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  5570,  -486,  -486,  4001,   -43,  -486,   -35,    27,    85,
    1382,  -486,  5328,   -40,  -486,  -486,  -486,   -63,  -162,  -486,
    -486,  -486,  -105,   -32,   -26,  2178,   -23,    14,   -12,  3495,
      25,    29,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  4421,
    4421,  4421,  -486,  -486,  -486,  -486,  -486,    13,  -486,  -486,
    -486,   -67,  -486,  -486,  -486,     9,   -99,  4631,    32,  -103,
    4421,    12,   -41,   126,  -127,   122,    41,    34,    11,   138,
     143,  -164,  -486,  -486,  -123,  -486,    50,    72,  -486,  -486,
    -486,  -486,   844,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  -486,  -486,    85,  -486,  -486,  -486,  -211,  3235,  -165,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,    70,  -486,
    4211,  5328,  -486,   -68,  -116,  -486,  -486,  -486,  1639,  -486,
    4421,  -486,  -119,  -486,    85,  -486,  -486,   170,  2715,  4421,
    -486,  -486,  -113,  4421,  3757,  -486,  -486,   -75,  -486,  2178,
    -486,  -486,   160,  4421,  -486,  -486,  4421,    75,  -486,  -486,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    -486,  4421,  -486,  4421,  4421,  4421,  4421,  4421,  4421,  4421,
    4421,  4421,  4421,  4421,  4421,  4421,  4421,  4421,  4421,  4421,
    4421,  4421,  4421,  4421,  -486,  -486,  -486,   -68,  3235,  -161,
    3235,  -486,  -486,  3235,  -486,  -486,    74,    85,    56,  5328,
     -43,    85,  -486,  -486,  -486,  -486,  -486,    79,  -486,  -486,
    3757,   -65,  -486,   -61,    76,    85,    80,  -486,  1120,  -486,
      81,    76,  -486,  -486,  -486,  -486,  -486,    12,    12,   -41,
     -41,   126,   126,   126,   126,  -127,  -127,   122,    41,    34,
      11,   138,   143,  -234,  -486,   -43,  -486,  3235,  -486,  -226,
    -486,  -486,  -111,   175,  -486,  -486,  4421,  -486,    66,    87,
    2178,    68,    71,  2453,  -486,  -486,  4421,  -486,  2975,  -486,
    -486,   -68,    77,   -53,  4421,  2453,   333,  -486,   -15,  -486,
    3235,  -486,  -486,  -486,  -486,  -486,  -486,   -43,  -486,    78,
      76,  -486,  2178,  4421,    82,  -486,  -486,  1903,  2178,   -10,
    -486,  -486,  -486,  -149,  -486,  -486,  -486,  -486,  -486,  2178,
    -486
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       4,     0,     0,    12,     0,     1,     2,    14,    15,    16,
       5,     0,     0,     0,    13,     6,     0,   182,   181,   209,
     205,   207,   208,   206,   188,   213,   214,   215,   216,   217,
     218,   219,   220,   221,   210,   211,   212,   222,   223,   224,
     318,   322,   319,   320,   321,   323,   324,   325,   178,   184,
     185,   186,   187,   183,   179,   159,   158,   157,   225,   226,
     227,   228,   229,   230,   231,   232,   233,   234,   235,   236,
     237,   238,   239,   240,   241,   242,   243,   244,   246,   247,
     249,   250,   252,   253,   254,   255,   256,   258,   259,   260,
     261,   263,   264,   265,   266,   268,   269,   270,   272,   273,
     274,   275,   277,   245,   262,   271,   251,   257,   267,   276,
     278,   279,   280,   281,   282,   283,   248,   284,   285,   286,
     287,   288,   289,   290,   291,   292,   293,   294,   295,   296,
     297,   298,   299,   300,   301,   302,   303,   304,   305,   306,
     307,   308,   309,   310,   311,   312,   313,   314,   315,   316,
     190,   191,   192,   193,   194,   317,   189,     0,   204,   203,
     160,   161,   328,   327,   326,     0,     0,     0,     0,     0,
       0,     0,   180,   153,   395,     3,   394,     0,     0,   112,
     120,     0,   130,   135,   165,   167,   164,     0,   162,   163,
     166,   142,   199,   201,   168,   202,    18,   393,   109,   398,
       0,   421,   422,   423,   424,   396,     0,     0,     0,   188,
     184,   185,   187,    21,    22,   160,   161,   140,   165,   170,
     162,   166,   141,   169,     0,     7,     8,     9,    10,    11,
       0,     0,    20,    19,   106,     0,   397,   110,   120,   120,
     126,   127,   128,   120,   113,     0,   120,   120,   120,     0,
     107,    14,    16,   136,     0,   188,   184,   185,   187,   172,
     399,   413,   415,   417,   419,   173,   171,   143,   174,   406,
     175,   165,   177,   400,     0,   200,   176,     0,     0,     0,
       0,   331,     0,     0,   152,   151,   150,   147,     0,   145,
     149,   155,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    28,    29,    24,    25,    30,    26,    27,     0,
       0,     0,    54,    55,    56,    57,   361,   353,   357,    23,
      32,    50,    34,    39,    40,     0,     0,    44,     0,    58,
       0,    62,    65,    68,    73,    76,    78,    80,    82,    84,
      86,    88,    90,   103,     0,   343,     0,   142,   346,   359,
     345,   344,     0,   347,   348,   349,   350,   351,   114,   121,
     122,   118,   119,   129,   123,   125,   124,   131,     0,   137,
     115,   416,   418,   420,   414,   195,    58,   105,     0,    48,
       0,     0,    17,   336,     0,   334,   330,   332,     0,   108,
       0,   144,     0,   154,     0,   389,   388,     0,     0,     0,
     392,   390,     0,     0,     0,    51,    52,     0,   352,     0,
      36,    37,     0,     0,    42,    41,     0,   204,    45,    47,
      93,    94,    96,    95,    98,    99,   100,   101,   102,    97,
      92,     0,    53,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   362,   358,   360,   116,     0,   132,
       0,   338,   139,     0,   196,   197,     0,     0,     0,   410,
     337,     0,   333,   329,   148,   146,   156,     0,   383,   382,
     385,     0,   391,     0,   366,     0,     0,    31,     0,    35,
       0,    38,    46,    91,    59,    60,    61,    63,    64,    66,
      67,    71,    72,    69,    70,    74,    75,    77,    79,    81,
      83,    85,    87,     0,   104,   117,   134,     0,   341,     0,
     138,   198,     0,   407,   411,   335,     0,   384,     0,     0,
       0,     0,     0,     0,   354,    33,     0,   133,     0,   339,
     412,   408,     0,     0,   386,     0,   365,   363,     0,   368,
       0,   356,   379,   355,    89,   340,   342,   409,   401,     0,
     387,   381,     0,     0,     0,   369,   373,     0,   377,     0,
     367,   380,   364,     0,   372,   375,   374,   376,   370,   378,
     371
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -486,  -486,  -486,  -486,  -486,  -486,    10,   173,  -486,   -70,
    -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,  -486,
    5357,  -486,  -134,  -118,  -146,  -121,   -91,   -90,   -88,   -87,
     -92,   -85,  -486,  -262,  -141,  -486,  -283,  -286,     5,     8,
    -486,  -486,  -486,  -486,   121,     6,  -486,  -486,  -486,  -486,
    -204,   -11,  -486,   -28,  -486,  -486,  -486,  -486,  -115,  -153,
    -486,    -9,  -252,   -13,  -486,  -486,   -29,  -486,    83,  -265,
    -106,  -104,  -405,  -486,   -27,  -275,  -485,  -486,  -486,  -182,
     192,   -37,   -24,  -486,  -486,  -110,  -486,  -486,  -190,  -486,
    -191,  -486,  -486,  -486,  -486,  -486,  -486,   204,  -486,  -133,
    -486,   193,  -486,   -83,  -486,   196,   198,   199,   200,  -486
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     2,    13,     3,   174,     6,   383,    14,   175,   319,
     320,   321,   490,   322,   323,   324,   325,   326,   327,   328,
     329,   330,   331,   332,   333,   334,   335,   336,   337,   338,
     339,   340,   341,   342,   343,   431,   344,   378,   345,   346,
     178,   179,   180,   361,   244,   245,   246,   362,   181,   182,
     183,   218,   288,   289,   290,   185,   292,   186,   187,   188,
     189,   221,   275,   379,   192,   193,   194,   195,   280,   281,
     384,   385,   462,   519,   348,   349,   350,   351,   409,   552,
     553,   352,   353,   354,   547,   486,   355,   549,   566,   567,
     568,   569,   356,   480,   528,   529,   357,   196,   197,   198,
     199,   200,   542,   468,   469,   201,   202,   203,   204,   205
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     191,   369,   184,   279,   190,   563,   564,   220,   220,  -405,
     563,   564,   377,  -402,    11,   387,   402,   451,   176,  -403,
     397,   177,    16,     7,     8,     9,   453,  -404,   407,     7,
       8,     9,   220,   220,   538,     4,   220,     1,   284,   274,
     536,   220,     7,     8,     9,   219,   223,   274,   551,   440,
     441,   260,   539,   516,   220,   518,   368,   273,   520,     5,
     551,    12,   220,   220,   458,   220,   206,   220,   220,   259,
     265,   266,    10,   268,   270,   272,   279,   456,   279,   276,
     420,   421,   422,   423,   424,   425,   426,   427,   428,   429,
     217,   222,    15,   380,   466,   391,   225,   380,   392,  -405,
     219,   223,   285,   259,   474,   268,   272,   410,   411,   452,
     463,   453,   537,   254,   517,   459,   481,  -402,   377,  -403,
     483,   484,   371,   387,  -404,   580,   372,   220,   377,   220,
     491,   470,   373,   556,   249,   286,   224,   453,   260,   207,
     374,   442,   443,   237,   471,   570,   412,   453,   226,   471,
     250,   248,   393,   454,   227,   394,   259,   228,   415,   239,
     472,   416,   191,   482,   184,   540,   190,   208,   229,   513,
     230,   247,   430,   184,   267,   190,   231,   467,   238,   271,
     176,   190,   487,   177,   279,   453,   418,   234,   235,   -49,
     274,   413,   530,   253,   191,   453,   531,   484,   277,   453,
     485,   240,   241,   242,   559,   515,   282,   453,   -21,   248,
     248,   283,   390,   456,   248,   380,   278,   248,   248,   248,
     436,   437,   347,   -22,   271,   370,   190,   461,   220,   247,
     247,   382,   363,   398,   247,   220,   389,   247,   247,   247,
     287,   291,   381,   543,   395,   359,     7,     8,     9,   360,
     396,   220,   364,   365,   366,   546,   162,   163,   164,   367,
     271,   560,   190,   565,   400,   467,   414,   191,   578,   191,
     399,   239,   251,     8,   252,   492,   485,   433,   434,   435,
     573,   403,   347,   448,   271,   404,   190,   572,   419,   557,
     493,   408,   575,   577,   501,   502,   503,   504,   438,   439,
     444,   445,   497,   498,   577,   447,   150,   151,   152,   153,
     154,   446,   514,   240,   241,   242,   220,   461,   449,   461,
     499,   500,   461,   505,   506,   450,   234,   220,   -48,   464,
     477,   489,   -43,   521,   523,   526,   453,   533,   541,   347,
     535,   271,   544,   190,   545,   548,   550,   562,   232,   243,
     162,   163,   164,   558,   571,   507,   574,   508,   511,   358,
     509,   522,   510,   561,   475,   388,   512,   525,   191,   236,
     527,   478,   488,   457,   479,   191,   461,   576,   579,   233,
     261,   269,   262,   263,   264,   347,   524,   271,     0,   190,
       0,   347,     0,     0,     0,   554,   347,   461,   271,     0,
     190,     0,   287,     0,   476,     0,     0,     0,     0,   461,
       0,     0,  -111,     0,     0,     0,     0,     0,   150,   151,
     152,   153,   154,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   191,     0,     0,     0,
       0,   243,   162,   163,   164,     0,     0,   347,     0,     0,
       0,     0,     0,     0,     0,   347,     0,   271,     0,   190,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   532,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   347,     0,   271,
     347,   190,   271,     0,   190,     0,     0,     0,     0,     0,
       0,     0,   347,     0,   271,     0,   190,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   347,
       0,   271,     0,   190,   347,   347,   271,   271,   190,   190,
       0,     0,     0,     0,     0,     0,   347,     0,   271,     0,
     190,    17,    18,    19,    20,    21,    22,    23,   293,    24,
     294,   295,     0,   296,   297,   298,   299,   300,     0,     0,
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
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,     0,     0,     0,     0,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   301,   213,
     159,   214,   302,   303,   304,   305,   306,   307,   308,     0,
       0,     0,   309,   310,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   160,   161,   162,   163,   164,     0,   165,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     171,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   172,   173,     0,     0,     0,
       0,     0,     0,     0,   311,     0,     0,     0,     0,   312,
     313,   314,   315,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   316,   317,   318,    17,    18,    19,
      20,    21,    22,    23,   293,    24,   294,   295,     0,   296,
     297,   298,   299,   300,     0,     0,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,     0,     0,     0,     0,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   301,   213,   159,   214,   302,   303,
     304,   305,   306,   307,   308,     0,     0,     0,   309,   310,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   160,   161,   162,
     163,   164,     0,   165,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   171,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   172,   173,     0,     0,     0,     0,     0,     0,     0,
     311,     0,     0,     0,     0,   312,   313,   314,   315,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     316,   317,   455,    17,    18,    19,    20,    21,    22,    23,
     293,    24,   294,   295,     0,   296,   297,   298,   299,   300,
       0,     0,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,     0,     0,     0,
       0,   150,   151,   152,   153,   154,   155,   156,   157,   158,
     301,   213,   159,   214,   302,   303,   304,   305,   306,   307,
     308,     0,     0,     0,   309,   310,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   160,   161,   162,   163,   164,     0,   165,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   171,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   172,   173,     0,
       0,     0,     0,     0,     0,     0,   311,     0,     0,     0,
       0,   312,   313,   314,   315,    17,    18,    19,    20,    21,
      22,    23,     0,   209,     0,     0,   316,   317,   534,     0,
       0,     0,     0,     0,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,   210,   211,
      51,   212,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,     0,
       0,     0,     0,   150,   151,   152,   153,   154,   155,   156,
     157,   158,     0,     0,   159,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   215,   216,   162,   163,   164,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   171,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   172,
     173,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,     0,
     209,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     386,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,   210,   211,    51,   212,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,     0,     0,     0,     0,
     150,   151,   152,   153,   154,   155,   156,   157,   158,     0,
       0,   159,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   215,   216,   162,   163,   164,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   171,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   172,   173,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    17,    18,    19,    20,
      21,    22,    23,   293,    24,   294,   295,   473,   296,   297,
     298,   299,   300,   563,   564,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
       0,     0,     0,     0,   150,   151,   152,   153,   154,   155,
     156,   157,   158,   301,   213,   159,   214,   302,   303,   304,
     305,   306,   307,   308,     0,     0,     0,   309,   310,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   160,   161,   162,   163,
     164,     0,   165,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   171,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     172,   173,     0,     0,     0,     0,     0,     0,     0,   311,
       0,     0,     0,     0,   312,   313,   314,   315,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   316,
     317,    17,    18,    19,    20,    21,    22,    23,   293,    24,
     294,   295,     0,   296,   297,   298,   299,   300,     0,     0,
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
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,     0,     0,     0,     0,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   301,   213,
     159,   214,   302,   303,   304,   305,   306,   307,   308,     0,
       0,     0,   309,   310,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   160,   161,   162,   163,   164,     0,   165,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     171,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   172,   173,     0,     0,     0,
       0,     0,     0,     0,   311,     0,     0,     0,     0,   312,
     313,   314,   315,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   316,   317,    17,    18,    19,    20,
      21,    22,    23,   293,    24,   294,   295,     0,   296,   297,
     298,   299,   300,     0,     0,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
       0,     0,     0,     0,   150,   151,   152,   153,   154,   155,
     156,   157,   158,   301,   213,   159,   214,   302,   303,   304,
     305,   306,   307,   308,     0,     0,     0,   309,   310,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   160,   161,   162,   163,
     164,     0,   165,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   171,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     172,   173,     0,     0,     0,     0,     0,     0,     0,   311,
       0,     0,     0,     0,   312,   313,   314,   315,    17,    18,
      19,    20,    21,    22,    23,     0,    24,     0,     0,   316,
     235,     0,     0,     0,     0,     0,     0,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,     0,     0,     0,     0,   150,   151,   152,   153,
     154,   155,   156,   157,   158,     0,   213,   159,   214,   302,
     303,   304,   305,   306,   307,   308,     0,     0,     0,   309,
     310,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   160,   161,
     162,   163,   164,     0,   165,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   171,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   172,   173,     0,     0,     0,     0,     0,     0,
       0,   311,     0,     0,     0,     0,   312,   313,   314,   315,
      19,    20,    21,    22,    23,     0,     0,     0,     0,     0,
       0,   316,     0,     0,     0,     0,     0,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   155,     0,   157,   158,     0,   213,   159,   214,   302,
     303,   304,   305,   306,   307,   308,     0,     0,     0,   309,
     310,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   311,     0,     0,     0,     0,   312,   313,   314,   315,
      19,    20,    21,    22,    23,     0,     0,     0,     0,     0,
       0,     0,   460,   555,     0,     0,     0,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   155,     0,   157,   158,     0,   213,   159,   214,   302,
     303,   304,   305,   306,   307,   308,     0,     0,     0,   309,
     310,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   311,     0,     0,     0,     0,   312,   313,   314,   315,
      19,    20,    21,    22,    23,     0,     0,     0,     0,     0,
       0,     0,   460,     0,     0,     0,     0,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   155,     0,   157,   158,     0,   213,   159,   214,   302,
     303,   304,   305,   306,   307,   308,     0,     0,     0,   309,
     310,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   311,     0,     0,     0,     0,   312,   313,   314,   315,
      17,    18,    19,    20,    21,    22,    23,     0,   209,     0,
       0,   401,     0,     0,     0,     0,     0,     0,     0,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,   210,   211,    51,   212,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,     0,     0,     0,     0,   150,   151,
     152,   153,   154,   155,   156,   157,   158,     0,   213,   159,
     214,   302,   303,   304,   305,   306,   307,   308,     0,     0,
       0,   309,   310,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     215,   216,   162,   163,   164,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   171,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   172,   173,    19,    20,    21,    22,
      23,     0,     0,   311,     0,     0,     0,     0,   312,   313,
     314,   315,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   155,     0,   157,
     158,     0,   213,   159,   214,   302,   303,   304,   305,   306,
     307,   308,     0,     0,     0,   309,   310,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,     0,   311,     0,     0,
     375,     0,   312,   313,   314,   315,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   155,     0,   157,
     158,     0,   213,   159,   214,   302,   303,   304,   305,   306,
     307,   308,     0,     0,     0,   309,   310,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,     0,   311,     0,     0,
     465,     0,   312,   313,   314,   315,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   155,     0,   157,
     158,     0,   213,   159,   214,   302,   303,   304,   305,   306,
     307,   308,     0,     0,     0,   309,   310,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,     0,   311,     0,     0,
       0,     0,   312,   313,   314,   315,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   155,     0,   157,
     417,     0,   213,   159,   214,   302,   303,   304,   305,   306,
     307,   308,     0,     0,     0,   309,   310,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,     0,    24,     0,   311,     0,     0,
       0,     0,   312,   313,   314,   315,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,     0,     0,     0,     0,   150,   151,   152,   153,   154,
     155,   156,   157,   158,     0,     0,   159,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   160,   161,   162,
     163,   164,     0,   165,     0,    12,     0,     0,     0,     0,
       0,   166,   167,   168,   169,   170,   171,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,     0,    24,     0,     0,     0,     0,     0,     0,     0,
       0,   172,   173,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,     0,     0,
       0,     0,   150,   151,   152,   153,   154,   155,   156,   157,
     158,     0,     0,   159,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   160,   161,   162,   163,   164,     0,
     165,     0,     0,     0,     0,     0,     0,     0,   166,   167,
     168,   169,   170,   171,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,     0,   209,
       0,     0,     0,     0,     0,     0,     0,     0,   172,   173,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,   210,   211,    51,   212,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,     0,     0,     0,     0,   150,
     151,   152,   153,   154,   155,   156,   157,   158,    17,    18,
     159,     0,     0,     0,     0,     0,   209,     0,     0,     0,
       0,    17,    18,     0,     0,     0,     0,     0,     0,   255,
       0,    17,    18,     0,     0,     0,     0,     0,     0,   209,
       0,   215,   216,   162,   163,   164,     0,     0,     0,     0,
      48,   210,   211,    51,   212,    53,    54,    55,    56,    57,
     171,     0,     0,    48,   256,   257,    51,   258,    53,    54,
      55,    56,    57,    48,   210,   211,    51,   212,    53,    54,
      55,    56,    57,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    17,    18,   172,   173,     0,     0,     0,
       0,    24,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,     0,     0,     0,     0,     0,
       0,   376,     0,     0,     0,     0,   150,   151,   152,   153,
     154,     0,   156,     0,     0,     0,   213,     0,   214,   150,
     151,   152,   153,   154,     0,   156,     0,     0,     0,   150,
     151,   152,   153,   154,     0,   156,   405,   406,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   215,   216,
     162,   163,   164,     0,     0,     0,     0,   432,     0,     0,
       0,   215,   216,   162,   163,   164,     0,   171,     0,     0,
       0,   215,   216,   162,   163,   164,     0,     0,     0,     0,
     171,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     171,   150,   151,   152,   153,   154,     0,   156,     0,     0,
       0,     0,   172,   173,     0,     0,     0,   376,     0,     0,
       0,     0,     0,     0,     0,   172,   173,   376,     0,     0,
       0,     0,     0,     0,     0,   172,   173,     0,     0,     0,
       0,     0,     0,   215,   216,   162,   163,   164,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   171,     0,     0,     0,     0,     0,     0,     0,
     494,   495,   496,   376,   376,   376,   376,   376,   376,   376,
     376,   376,   376,   376,   376,   376,   376,   376,   376,    19,
      20,    21,    22,    23,     0,     0,     0,   172,   173,     0,
       0,     0,     0,     0,     0,     0,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     155,     0,   157,   158,     0,     0,   159
};

static const yytype_int16 yycheck[] =
{
      13,   253,    13,   207,    13,    20,    21,   160,   161,   163,
      20,    21,   274,   163,     4,   280,   299,   181,    13,   163,
     295,    13,    12,   161,   162,   163,   260,   163,   311,   161,
     162,   163,   185,   186,   260,   166,   189,   200,   157,   258,
     274,   194,   161,   162,   163,   160,   161,   258,   533,   176,
     177,   184,   278,   458,   207,   460,   275,   190,   463,     0,
     545,   201,   215,   216,   275,   218,   203,   220,   221,   184,
     185,   186,   204,   188,   189,   190,   280,   352,   282,   194,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     160,   161,   204,   258,   380,   257,   204,   258,   260,   163,
     215,   216,   221,   218,   390,   220,   221,   174,   175,   273,
     275,   260,   517,   183,   275,   367,   399,   163,   380,   163,
     403,   404,   276,   388,   163,   274,   276,   280,   390,   282,
     413,   383,   276,   538,   260,   254,   165,   260,   271,   277,
     276,   268,   269,   257,   260,   550,   213,   260,   204,   260,
     276,   180,   257,   276,   204,   260,   271,   204,   257,     4,
     276,   260,   175,   276,   175,   276,   175,   157,   204,   452,
     256,   180,   275,   184,   187,   184,   256,   381,   260,   190,
     175,   190,   257,   175,   388,   260,   327,   276,   277,   256,
     258,   258,   257,   183,   207,   260,   257,   480,   163,   260,
     404,    46,    47,    48,   257,   457,   277,   260,   256,   238,
     239,   224,   275,   488,   243,   258,   206,   246,   247,   248,
     261,   262,   235,   256,   235,   256,   235,   368,   381,   238,
     239,   204,   245,   256,   243,   388,   276,   246,   247,   248,
     230,   231,   277,   526,   276,   239,   161,   162,   163,   243,
     276,   404,   246,   247,   248,   530,   195,   196,   197,   249,
     271,   544,   271,   278,   276,   469,   257,   280,   278,   282,
     256,     4,   161,   162,   163,   416,   480,   265,   266,   267,
     563,   256,   295,   272,   295,   256,   295,   562,   256,   541,
     431,   278,   567,   568,   440,   441,   442,   443,   172,   173,
     178,   179,   436,   437,   579,   271,   151,   152,   153,   154,
     155,   270,   453,    46,    47,    48,   469,   458,   180,   460,
     438,   439,   463,   444,   445,   182,   276,   480,   256,   259,
     160,   171,   257,   259,   278,   256,   260,   257,   163,   352,
     259,   352,   276,   352,   257,   277,   275,    14,   175,   194,
     195,   196,   197,   276,   276,   446,   274,   447,   450,   238,
     448,   467,   449,   545,   392,   282,   451,   471,   381,   177,
     480,   398,   409,   363,   398,   388,   517,   567,   569,   175,
     184,   188,   184,   184,   184,   398,   469,   398,    -1,   398,
      -1,   404,    -1,    -1,    -1,   536,   409,   538,   409,    -1,
     409,    -1,   392,    -1,   394,    -1,    -1,    -1,    -1,   550,
      -1,    -1,   257,    -1,    -1,    -1,    -1,    -1,   151,   152,
     153,   154,   155,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   469,    -1,    -1,    -1,
      -1,   194,   195,   196,   197,    -1,    -1,   480,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   488,    -1,   488,    -1,   488,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   485,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   530,    -1,   530,
     533,   530,   533,    -1,   533,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   545,    -1,   545,    -1,   545,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   562,
      -1,   562,    -1,   562,   567,   568,   567,   568,   567,   568,
      -1,    -1,    -1,    -1,    -1,    -1,   579,    -1,   579,    -1,
     579,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    -1,    15,    16,    17,    18,    19,    -1,    -1,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,    -1,    -1,    -1,    -1,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,    -1,
      -1,    -1,   174,   175,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   193,   194,   195,   196,   197,    -1,   199,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     212,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   247,   248,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   256,    -1,    -1,    -1,    -1,   261,
     262,   263,   264,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   276,   277,   278,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    -1,    15,
      16,    17,    18,    19,    -1,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,    -1,    -1,    -1,    -1,   151,   152,   153,   154,   155,
     156,   157,   158,   159,   160,   161,   162,   163,   164,   165,
     166,   167,   168,   169,   170,    -1,    -1,    -1,   174,   175,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   193,   194,   195,
     196,   197,    -1,   199,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   212,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   247,   248,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     256,    -1,    -1,    -1,    -1,   261,   262,   263,   264,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     276,   277,   278,     3,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    -1,    15,    16,    17,    18,    19,
      -1,    -1,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,    -1,    -1,    -1,
      -1,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,    -1,    -1,    -1,   174,   175,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   193,   194,   195,   196,   197,    -1,   199,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   212,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   247,   248,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   256,    -1,    -1,    -1,
      -1,   261,   262,   263,   264,     3,     4,     5,     6,     7,
       8,     9,    -1,    11,    -1,    -1,   276,   277,   278,    -1,
      -1,    -1,    -1,    -1,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,    -1,
      -1,    -1,    -1,   151,   152,   153,   154,   155,   156,   157,
     158,   159,    -1,    -1,   162,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   193,   194,   195,   196,   197,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   212,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   247,
     248,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,     3,     4,     5,     6,     7,     8,     9,    -1,
      11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     278,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,    -1,    -1,    -1,    -1,
     151,   152,   153,   154,   155,   156,   157,   158,   159,    -1,
      -1,   162,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   193,   194,   195,   196,   197,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   212,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   247,   248,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,   278,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
      -1,    -1,    -1,    -1,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,    -1,    -1,    -1,   174,   175,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   193,   194,   195,   196,
     197,    -1,   199,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   212,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     247,   248,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   256,
      -1,    -1,    -1,    -1,   261,   262,   263,   264,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   276,
     277,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    -1,    15,    16,    17,    18,    19,    -1,    -1,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,    -1,    -1,    -1,    -1,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,    -1,
      -1,    -1,   174,   175,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   193,   194,   195,   196,   197,    -1,   199,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     212,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   247,   248,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   256,    -1,    -1,    -1,    -1,   261,
     262,   263,   264,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   276,   277,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    -1,    15,    16,
      17,    18,    19,    -1,    -1,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
      -1,    -1,    -1,    -1,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,    -1,    -1,    -1,   174,   175,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   193,   194,   195,   196,
     197,    -1,   199,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   212,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     247,   248,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   256,
      -1,    -1,    -1,    -1,   261,   262,   263,   264,     3,     4,
       5,     6,     7,     8,     9,    -1,    11,    -1,    -1,   276,
     277,    -1,    -1,    -1,    -1,    -1,    -1,    22,    23,    24,
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
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,    -1,    -1,    -1,    -1,   151,   152,   153,   154,
     155,   156,   157,   158,   159,    -1,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,    -1,    -1,    -1,   174,
     175,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   193,   194,
     195,   196,   197,    -1,   199,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   212,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   247,   248,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   256,    -1,    -1,    -1,    -1,   261,   262,   263,   264,
       5,     6,     7,     8,     9,    -1,    -1,    -1,    -1,    -1,
      -1,   276,    -1,    -1,    -1,    -1,    -1,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   156,    -1,   158,   159,    -1,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,    -1,    -1,    -1,   174,
     175,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   256,    -1,    -1,    -1,    -1,   261,   262,   263,   264,
       5,     6,     7,     8,     9,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   277,   278,    -1,    -1,    -1,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   156,    -1,   158,   159,    -1,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,    -1,    -1,    -1,   174,
     175,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   256,    -1,    -1,    -1,    -1,   261,   262,   263,   264,
       5,     6,     7,     8,     9,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   277,    -1,    -1,    -1,    -1,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   156,    -1,   158,   159,    -1,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,    -1,    -1,    -1,   174,
     175,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   256,    -1,    -1,    -1,    -1,   261,   262,   263,   264,
       3,     4,     5,     6,     7,     8,     9,    -1,    11,    -1,
      -1,   276,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,    -1,    -1,    -1,    -1,   151,   152,
     153,   154,   155,   156,   157,   158,   159,    -1,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,    -1,    -1,
      -1,   174,   175,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     193,   194,   195,   196,   197,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   212,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   247,   248,     5,     6,     7,     8,
       9,    -1,    -1,   256,    -1,    -1,    -1,    -1,   261,   262,
     263,   264,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   156,    -1,   158,
     159,    -1,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,    -1,    -1,    -1,   174,   175,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    -1,   256,    -1,    -1,
     259,    -1,   261,   262,   263,   264,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   156,    -1,   158,
     159,    -1,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,    -1,    -1,    -1,   174,   175,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    -1,   256,    -1,    -1,
     259,    -1,   261,   262,   263,   264,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   156,    -1,   158,
     159,    -1,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,    -1,    -1,    -1,   174,   175,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    -1,   256,    -1,    -1,
      -1,    -1,   261,   262,   263,   264,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   156,    -1,   158,
     159,    -1,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,    -1,    -1,    -1,   174,   175,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,
       6,     7,     8,     9,    -1,    11,    -1,   256,    -1,    -1,
      -1,    -1,   261,   262,   263,   264,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,    -1,    -1,    -1,    -1,   151,   152,   153,   154,   155,
     156,   157,   158,   159,    -1,    -1,   162,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   193,   194,   195,
     196,   197,    -1,   199,    -1,   201,    -1,    -1,    -1,    -1,
      -1,   207,   208,   209,   210,   211,   212,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,     8,
       9,    -1,    11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   247,   248,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,    -1,    -1,
      -1,    -1,   151,   152,   153,   154,   155,   156,   157,   158,
     159,    -1,    -1,   162,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   193,   194,   195,   196,   197,    -1,
     199,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   207,   208,
     209,   210,   211,   212,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,     3,     4,     5,     6,     7,     8,     9,    -1,    11,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   247,   248,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,    -1,    -1,    -1,    -1,   151,
     152,   153,   154,   155,   156,   157,   158,   159,     3,     4,
     162,    -1,    -1,    -1,    -1,    -1,    11,    -1,    -1,    -1,
      -1,     3,     4,    -1,    -1,    -1,    -1,    -1,    -1,    11,
      -1,     3,     4,    -1,    -1,    -1,    -1,    -1,    -1,    11,
      -1,   193,   194,   195,   196,   197,    -1,    -1,    -1,    -1,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
     212,    -1,    -1,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     3,     4,   247,   248,    -1,    -1,    -1,
      -1,    11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    -1,    -1,    -1,    -1,    -1,
      -1,   274,    -1,    -1,    -1,    -1,   151,   152,   153,   154,
     155,    -1,   157,    -1,    -1,    -1,   161,    -1,   163,   151,
     152,   153,   154,   155,    -1,   157,    -1,    -1,    -1,   151,
     152,   153,   154,   155,    -1,   157,   309,   310,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   193,   194,
     195,   196,   197,    -1,    -1,    -1,    -1,   330,    -1,    -1,
      -1,   193,   194,   195,   196,   197,    -1,   212,    -1,    -1,
      -1,   193,   194,   195,   196,   197,    -1,    -1,    -1,    -1,
     212,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     212,   151,   152,   153,   154,   155,    -1,   157,    -1,    -1,
      -1,    -1,   247,   248,    -1,    -1,    -1,   380,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   247,   248,   390,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   247,   248,    -1,    -1,    -1,
      -1,    -1,    -1,   193,   194,   195,   196,   197,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   212,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     433,   434,   435,   436,   437,   438,   439,   440,   441,   442,
     443,   444,   445,   446,   447,   448,   449,   450,   451,     5,
       6,     7,     8,     9,    -1,    -1,    -1,   247,   248,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     156,    -1,   158,   159,    -1,    -1,   162
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint16 yystos[] =
{
       0,   200,   280,   282,   166,     0,   284,   161,   162,   163,
     204,   285,   201,   281,   286,   204,   285,     3,     4,     5,
       6,     7,     8,     9,    11,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   162,
     193,   194,   195,   196,   197,   199,   207,   208,   209,   210,
     211,   212,   247,   248,   283,   287,   317,   318,   319,   320,
     321,   327,   328,   329,   330,   334,   336,   337,   338,   339,
     340,   342,   343,   344,   345,   346,   376,   377,   378,   379,
     380,   384,   385,   386,   387,   388,   203,   277,   285,    11,
      46,    47,    49,   161,   163,   193,   194,   288,   330,   337,
     338,   340,   288,   337,   345,   204,   204,   204,   204,   204,
     256,   256,   286,   376,   276,   277,   359,   257,   260,     4,
      46,    47,    48,   194,   323,   324,   325,   340,   345,   260,
     276,   161,   163,   285,   288,    11,    46,    47,    49,   337,
     378,   384,   385,   386,   387,   337,   337,   342,   337,   380,
     337,   330,   337,   378,   258,   341,   337,   163,   285,   329,
     347,   348,   277,   342,   157,   221,   254,   285,   331,   332,
     333,   285,   335,    10,    12,    13,    15,    16,    17,    18,
      19,   160,   164,   165,   166,   167,   168,   169,   170,   174,
     175,   256,   261,   262,   263,   264,   276,   277,   278,   288,
     289,   290,   292,   293,   294,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,   313,   315,   317,   318,   342,   353,   354,
     355,   356,   360,   361,   362,   365,   371,   375,   323,   324,
     324,   322,   326,   342,   324,   324,   324,   285,   275,   341,
     256,   276,   276,   276,   276,   259,   299,   312,   316,   342,
     258,   277,   204,   285,   349,   350,   278,   348,   347,   276,
     275,   257,   260,   257,   260,   276,   276,   354,   256,   256,
     276,   276,   315,   256,   256,   299,   299,   315,   278,   357,
     174,   175,   213,   258,   257,   257,   260,   159,   313,   256,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     275,   314,   299,   265,   266,   267,   261,   262,   172,   173,
     176,   177,   268,   269,   178,   179,   270,   271,   272,   180,
     182,   181,   273,   260,   276,   278,   354,   285,   275,   341,
     277,   313,   351,   275,   259,   259,   316,   329,   382,   383,
     341,   260,   276,   278,   316,   332,   285,   160,   353,   361,
     372,   315,   276,   315,   315,   329,   364,   257,   360,   171,
     291,   315,   313,   313,   299,   299,   299,   301,   301,   302,
     302,   303,   303,   303,   303,   304,   304,   305,   306,   307,
     308,   309,   310,   315,   313,   341,   351,   275,   351,   352,
     351,   259,   349,   278,   382,   350,   256,   364,   373,   374,
     257,   257,   285,   257,   278,   259,   274,   351,   260,   278,
     276,   163,   381,   315,   276,   257,   354,   363,   277,   366,
     275,   355,   358,   359,   313,   278,   351,   341,   276,   257,
     315,   358,    14,    20,    21,   278,   367,   368,   369,   370,
     351,   276,   354,   315,   274,   354,   367,   354,   278,   369,
     274
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint16 yyr1[] =
{
       0,   279,   281,   280,   282,   282,   282,   283,   283,   283,
     283,   283,   284,   284,   285,   285,   285,   286,   287,   287,
     287,   288,   288,   289,   289,   289,   289,   289,   289,   289,
     289,   289,   290,   290,   290,   290,   290,   290,   291,   292,
     293,   294,   294,   295,   295,   296,   296,   297,   298,   298,
     299,   299,   299,   299,   300,   300,   300,   300,   301,   301,
     301,   301,   302,   302,   302,   303,   303,   303,   304,   304,
     304,   304,   304,   305,   305,   305,   306,   306,   307,   307,
     308,   308,   309,   309,   310,   310,   311,   311,   312,   312,
     313,   313,   314,   314,   314,   314,   314,   314,   314,   314,
     314,   314,   314,   315,   315,   316,   317,   317,   317,   317,
     318,   319,   319,   320,   320,   321,   322,   322,   323,   323,
     324,   324,   324,   324,   324,   324,   325,   325,   325,   326,
     327,   327,   327,   327,   327,   328,   328,   328,   328,   328,
     328,   328,   329,   329,   330,   331,   331,   332,   332,   332,
     333,   333,   333,   334,   334,   335,   335,   336,   336,   336,
     337,   337,   337,   337,   337,   337,   337,   337,   337,   337,
     337,   337,   337,   337,   337,   337,   337,   337,   338,   338,
     338,   339,   339,   339,   339,   339,   339,   339,   339,   339,
     340,   340,   340,   340,   340,   341,   341,   341,   341,   342,
     342,   343,   343,   343,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   344,   344,   344,   344,
     344,   344,   344,   344,   344,   344,   345,   345,   345,   346,
     346,   347,   347,   348,   349,   349,   350,   350,   351,   351,
     351,   352,   352,   353,   354,   354,   355,   355,   355,   355,
     355,   355,   356,   357,   356,   358,   358,   359,   359,   360,
     360,   361,   361,   362,   363,   363,   364,   364,   365,   366,
     366,   367,   367,   368,   368,   369,   369,   370,   370,   371,
     371,   371,   372,   372,   373,   373,   374,   374,   375,   375,
     375,   375,   375,   376,   376,   376,   376,   377,   378,   378,
     378,   379,   380,   380,   380,   380,   380,   381,   381,   381,
     382,   382,   383,   384,   384,   385,   385,   386,   386,   387,
     387,   388,   388,   388,   388
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
       2,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     5,
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

#line 3164 "glsl/glsl_parser.cpp" /* yacc.c:1429  */
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
#line 313 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      _mesa_glsl_initialize_types(state);
   }
#line 3355 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 3:
#line 317 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 3377 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 5:
#line 339 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->process_version_directive(&(yylsp[-1]), (yyvsp[-1].n), NULL);
      if (state->error) {
         YYERROR;
      }
   }
#line 3388 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 6:
#line 346 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->process_version_directive(&(yylsp[-2]), (yyvsp[-2].n), (yyvsp[-1].identifier));
      if (state->error) {
         YYERROR;
      }
   }
#line 3399 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 11:
#line 360 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 3425 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 17:
#line 396 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!_mesa_glsl_process_extension((yyvsp[-3].identifier), & (yylsp[-3]), (yyvsp[-1].identifier), & (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 3435 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 18:
#line 405 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* FINISHME: The NULL test is required because pragmas are set to
       * FINISHME: NULL. (See production rule for external_declaration.)
       */
      if ((yyvsp[0].node) != NULL)
         state->translation_unit.push_tail(& (yyvsp[0].node)->link);
   }
#line 3447 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 19:
#line 413 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* FINISHME: The NULL test is required because pragmas are set to
       * FINISHME: NULL. (See production rule for external_declaration.)
       */
      if ((yyvsp[0].node) != NULL)
         state->translation_unit.push_tail(& (yyvsp[0].node)->link);
   }
#line 3459 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 20:
#line 420 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!state->allow_extension_directive_midshader) {
         _mesa_glsl_error(& (yylsp[0]), state,
                          "#extension directive is not allowed "
                          "in the middle of a shader");
         YYERROR;
      }
   }
#line 3472 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 23:
#line 437 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_identifier, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.identifier = (yyvsp[0].identifier);
   }
#line 3483 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 24:
#line 444 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_int_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.int_constant = (yyvsp[0].n);
   }
#line 3494 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 25:
#line 451 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_uint_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.uint_constant = (yyvsp[0].n);
   }
#line 3505 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 26:
#line 458 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_int64_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.int64_constant = (yyvsp[0].n64);
   }
#line 3516 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 27:
#line 465 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_uint64_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.uint64_constant = (yyvsp[0].n64);
   }
#line 3527 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 28:
#line 472 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_float_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.float_constant = (yyvsp[0].real);
   }
#line 3538 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 29:
#line 479 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_double_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.double_constant = (yyvsp[0].dreal);
   }
#line 3549 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 30:
#line 486 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_bool_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.bool_constant = (yyvsp[0].n);
   }
#line 3560 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 31:
#line 493 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
   }
#line 3568 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 33:
#line 501 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_array_index, (yyvsp[-3].expression), (yyvsp[-1].expression), NULL);
      (yyval.expression)->set_location_range((yylsp[-3]), (yylsp[0]));
   }
#line 3578 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 34:
#line 507 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[0].expression);
   }
#line 3586 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 35:
#line 511 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_field_selection, (yyvsp[-2].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.expression)->primary_expression.identifier = (yyvsp[0].identifier);
   }
#line 3597 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 36:
#line 518 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_post_inc, (yyvsp[-1].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 3607 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 37:
#line 524 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_post_dec, (yyvsp[-1].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 3617 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 45:
#line 555 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
      (yyval.expression)->set_location((yylsp[-1]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 3627 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 46:
#line 561 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-2].expression);
      (yyval.expression)->set_location((yylsp[-2]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 3637 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 48:
#line 577 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_function_expression((yyvsp[0].type_specifier));
      (yyval.expression)->set_location((yylsp[0]));
      }
#line 3647 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 49:
#line 583 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_function_expression((yyvsp[0].expression));
      (yyval.expression)->set_location((yylsp[0]));
      }
#line 3657 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 51:
#line 598 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_pre_inc, (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location((yylsp[-1]));
   }
#line 3667 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 52:
#line 604 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_pre_dec, (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location((yylsp[-1]));
   }
#line 3677 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 53:
#line 610 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression((yyvsp[-1].n), (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 3687 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 54:
#line 619 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_plus; }
#line 3693 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 55:
#line 620 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_neg; }
#line 3699 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 56:
#line 621 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_logic_not; }
#line 3705 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 57:
#line 622 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_bit_not; }
#line 3711 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 59:
#line 628 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_mul, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3721 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 60:
#line 634 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_div, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3731 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 61:
#line 640 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_mod, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3741 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 63:
#line 650 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_add, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3751 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 64:
#line 656 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_sub, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3761 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 66:
#line 666 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_lshift, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3771 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 67:
#line 672 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_rshift, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3781 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 69:
#line 682 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_less, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3791 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 70:
#line 688 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_greater, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3801 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 71:
#line 694 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_lequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3811 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 72:
#line 700 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_gequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3821 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 74:
#line 710 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_equal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3831 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 75:
#line 716 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_nequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3841 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 77:
#line 726 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_and, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3851 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 79:
#line 736 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_xor, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3861 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 81:
#line 746 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_or, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3871 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 83:
#line 756 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_and, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3881 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 85:
#line 766 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_xor, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3891 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 87:
#line 776 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_or, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3901 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 89:
#line 786 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression(ast_conditional, (yyvsp[-4].expression), (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 3911 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 91:
#line 796 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_expression((yyvsp[-1].n), (yyvsp[-2].expression), (yyvsp[0].expression), NULL);
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3921 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 92:
#line 804 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_assign; }
#line 3927 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 93:
#line 805 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_mul_assign; }
#line 3933 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 94:
#line 806 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_div_assign; }
#line 3939 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 95:
#line 807 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_mod_assign; }
#line 3945 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 96:
#line 808 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_add_assign; }
#line 3951 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 97:
#line 809 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_sub_assign; }
#line 3957 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 98:
#line 810 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_ls_assign; }
#line 3963 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 99:
#line 811 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_rs_assign; }
#line 3969 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 100:
#line 812 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_and_assign; }
#line 3975 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 101:
#line 813 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_xor_assign; }
#line 3981 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 102:
#line 814 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_or_assign; }
#line 3987 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 103:
#line 819 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[0].expression);
   }
#line 3995 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 104:
#line 823 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4012 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 106:
#line 843 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->symbols->pop_scope();
      (yyval.node) = (yyvsp[-1].function);
   }
#line 4021 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 107:
#line 848 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[-1].declarator_list);
   }
#line 4029 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 108:
#line 852 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyvsp[-1].type_specifier)->default_precision = (yyvsp[-2].n);
      (yyval.node) = (yyvsp[-1].type_specifier);
   }
#line 4038 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 109:
#line 857 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4056 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 113:
#line 883 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.function) = (yyvsp[-1].function);
      (yyval.function)->parameters.push_tail(& (yyvsp[0].parameter_declarator)->link);
   }
#line 4065 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 114:
#line 888 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.function) = (yyvsp[-2].function);
      (yyval.function)->parameters.push_tail(& (yyvsp[0].parameter_declarator)->link);
   }
#line 4074 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 115:
#line 896 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4093 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 116:
#line 914 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4108 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 117:
#line 925 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4124 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 118:
#line 940 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.parameter_declarator) = (yyvsp[0].parameter_declarator);
      (yyval.parameter_declarator)->type->qualifier = (yyvsp[-1].type_qualifier);
      if (!(yyval.parameter_declarator)->type->qualifier.push_to_global(& (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 4136 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 119:
#line 948 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4153 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 120:
#line 964 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
   }
#line 4161 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 121:
#line 968 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.constant)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate const qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.constant = 1;
   }
#line 4173 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 122:
#line 976 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precise qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 4185 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 123:
#line 984 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (((yyvsp[-1].type_qualifier).flags.q.in || (yyvsp[-1].type_qualifier).flags.q.out) && ((yyvsp[0].type_qualifier).flags.q.in || (yyvsp[0].type_qualifier).flags.q.out))
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate in/out/inout qualifier");

      if (!state->has_420pack_or_es31() && (yyvsp[0].type_qualifier).flags.q.constant)
         _mesa_glsl_error(&(yylsp[-1]), state, "in/out/inout must come after const "
                                      "or precise");

      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4201 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 124:
#line 996 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).precision != ast_precision_none)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precision qualifier");

      if (!state->has_420pack_or_es31() &&
          (yyvsp[0].type_qualifier).flags.i != 0)
         _mesa_glsl_error(&(yylsp[-1]), state, "precision qualifiers must come last");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).precision = (yyvsp[-1].n);
   }
#line 4217 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 125:
#line 1008 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4226 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 126:
#line 1015 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 4235 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 127:
#line 1020 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 4244 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 128:
#line 1025 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 4254 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 131:
#line 1039 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = (yyvsp[-2].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[0].identifier), ir_var_auto));
   }
#line 4268 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 132:
#line 1049 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      decl->set_location_range((yylsp[-1]), (yylsp[0]));

      (yyval.declarator_list) = (yyvsp[-3].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-1].identifier), ir_var_auto));
   }
#line 4282 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 133:
#line 1059 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-3].identifier), (yyvsp[-2].array_specifier), (yyvsp[0].expression));
      decl->set_location_range((yylsp[-3]), (yylsp[-2]));

      (yyval.declarator_list) = (yyvsp[-5].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-3].identifier), ir_var_auto));
   }
#line 4296 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 134:
#line 1069 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      decl->set_location((yylsp[-2]));

      (yyval.declarator_list) = (yyvsp[-4].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-2].identifier), ir_var_auto));
   }
#line 4310 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 135:
#line 1083 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      /* Empty declaration list is valid. */
      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[0].fully_specified_type));
      (yyval.declarator_list)->set_location((yylsp[0]));
   }
#line 4321 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 136:
#line 1090 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-1].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[0].identifier), ir_var_auto));
   }
#line 4336 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 137:
#line 1101 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      decl->set_location_range((yylsp[-1]), (yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-2].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-1].identifier), ir_var_auto));
   }
#line 4351 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 138:
#line 1112 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-3].identifier), (yyvsp[-2].array_specifier), (yyvsp[0].expression));
      decl->set_location_range((yylsp[-3]), (yylsp[-2]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-4].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-4]), (yylsp[-2]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-3].identifier), ir_var_auto));
   }
#line 4366 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 139:
#line 1123 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      decl->set_location((yylsp[-2]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-3].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-3]), (yylsp[-2]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-2].identifier), ir_var_auto));
   }
#line 4381 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 140:
#line 1134 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list(NULL);
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->invariant = true;

      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4397 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 141:
#line 1146 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list(NULL);
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->precise = true;

      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4413 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 142:
#line 1161 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.fully_specified_type) = new(ctx) ast_fully_specified_type();
      (yyval.fully_specified_type)->set_location((yylsp[0]));
      (yyval.fully_specified_type)->specifier = (yyvsp[0].type_specifier);
   }
#line 4424 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 143:
#line 1168 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4443 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 144:
#line 1186 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
   }
#line 4451 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 146:
#line 1194 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-2].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[0]), state, (yyvsp[0].type_qualifier), true)) {
         YYERROR;
      }
   }
#line 4462 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 147:
#line 1204 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4882 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 148:
#line 1620 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5051 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 149:
#line 1785 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5069 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 150:
#line 1811 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.row_major = 1;
   }
#line 5078 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 151:
#line 1816 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.packed = 1;
   }
#line 5087 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 152:
#line 1821 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.shared = 1;
   }
#line 5096 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 153:
#line 1829 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.subroutine = 1;
   }
#line 5105 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 154:
#line 1834 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.subroutine = 1;
      (yyval.type_qualifier).subroutine_list = (yyvsp[-1].subroutine_list);
   }
#line 5115 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 155:
#line 1843 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
        void *ctx = state->linalloc;
        ast_declaration *decl = new(ctx)  ast_declaration((yyvsp[0].identifier), NULL, NULL);
        decl->set_location((yylsp[0]));

        (yyval.subroutine_list) = new(ctx) ast_subroutine_list();
        (yyval.subroutine_list)->declarations.push_tail(&decl->link);
   }
#line 5128 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 156:
#line 1852 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
        void *ctx = state->linalloc;
        ast_declaration *decl = new(ctx)  ast_declaration((yyvsp[0].identifier), NULL, NULL);
        decl->set_location((yylsp[0]));

        (yyval.subroutine_list) = (yyvsp[-2].subroutine_list);
        (yyval.subroutine_list)->declarations.push_tail(&decl->link);
   }
#line 5141 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 157:
#line 1864 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.smooth = 1;
   }
#line 5150 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 158:
#line 1869 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.flat = 1;
   }
#line 5159 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 159:
#line 1874 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.noperspective = 1;
   }
#line 5168 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 160:
#line 1883 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.invariant = 1;
   }
#line 5177 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 161:
#line 1888 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 5186 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 168:
#line 1899 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(&(yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).precision = (yyvsp[0].n);
   }
#line 5195 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 169:
#line 1917 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate \"precise\" qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 5207 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 170:
#line 1925 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5236 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 171:
#line 1950 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5264 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 172:
#line 1974 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5282 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 173:
#line 1988 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 5291 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 174:
#line 1993 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5311 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 175:
#line 2009 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5335 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 176:
#line 2029 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).precision != ast_precision_none)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precision qualifier");

      if (!(state->has_420pack_or_es31()) &&
          (yyvsp[0].type_qualifier).flags.i != 0)
         _mesa_glsl_error(&(yylsp[-1]), state, "precision qualifiers must come last");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).precision = (yyvsp[-1].n);
   }
#line 5351 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 177:
#line 2041 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 5360 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 178:
#line 2049 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.centroid = 1;
   }
#line 5369 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 179:
#line 2054 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.sample = 1;
   }
#line 5378 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 180:
#line 2059 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.patch = 1;
   }
#line 5387 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 181:
#line 2066 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.constant = 1;
   }
#line 5396 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 182:
#line 2071 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.attribute = 1;
   }
#line 5405 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 183:
#line 2076 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.varying = 1;
   }
#line 5414 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 184:
#line 2081 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 5423 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 185:
#line 2086 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5452 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 186:
#line 2111 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5468 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 187:
#line 2123 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.uniform = 1;
   }
#line 5477 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 188:
#line 2128 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.buffer = 1;
   }
#line 5486 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 189:
#line 2133 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.shared_storage = 1;
   }
#line 5495 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 190:
#line 2141 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.coherent = 1;
   }
#line 5504 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 191:
#line 2146 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q._volatile = 1;
   }
#line 5513 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 192:
#line 2151 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      STATIC_ASSERT(sizeof((yyval.type_qualifier).flags.q) <= sizeof((yyval.type_qualifier).flags.i));
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.restrict_flag = 1;
   }
#line 5523 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 193:
#line 2157 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.read_only = 1;
   }
#line 5532 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 194:
#line 2162 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.write_only = 1;
   }
#line 5541 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 195:
#line 2170 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.array_specifier) = new(ctx) ast_array_specifier((yylsp[-1]), new(ctx) ast_expression(
                                                  ast_unsized_array_dim, NULL,
                                                  NULL, NULL));
      (yyval.array_specifier)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 5553 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 196:
#line 2178 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.array_specifier) = new(ctx) ast_array_specifier((yylsp[-2]), (yyvsp[-1].expression));
      (yyval.array_specifier)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 5563 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 197:
#line 2184 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.array_specifier) = (yyvsp[-2].array_specifier);

      if (state->check_arrays_of_arrays_allowed(& (yylsp[-2]))) {
         (yyval.array_specifier)->add_dimension(new(ctx) ast_expression(ast_unsized_array_dim, NULL,
                                                   NULL, NULL));
      }
   }
#line 5577 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 198:
#line 2194 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.array_specifier) = (yyvsp[-3].array_specifier);

      if (state->check_arrays_of_arrays_allowed(& (yylsp[-3]))) {
         (yyval.array_specifier)->add_dimension((yyvsp[-1].expression));
      }
   }
#line 5589 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 200:
#line 2206 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_specifier) = (yyvsp[-1].type_specifier);
      (yyval.type_specifier)->array_specifier = (yyvsp[0].array_specifier);
   }
#line 5598 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 201:
#line 2214 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].identifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 5608 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 202:
#line 2220 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].struct_specifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 5618 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 203:
#line 2226 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].identifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 5628 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 204:
#line 2234 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "void"; }
#line 5634 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 205:
#line 2235 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "float"; }
#line 5640 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 206:
#line 2236 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "double"; }
#line 5646 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 207:
#line 2237 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "int"; }
#line 5652 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 208:
#line 2238 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uint"; }
#line 5658 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 209:
#line 2239 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bool"; }
#line 5664 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 210:
#line 2240 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "vec2"; }
#line 5670 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 211:
#line 2241 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "vec3"; }
#line 5676 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 212:
#line 2242 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "vec4"; }
#line 5682 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 213:
#line 2243 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bvec2"; }
#line 5688 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 214:
#line 2244 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bvec3"; }
#line 5694 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 215:
#line 2245 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bvec4"; }
#line 5700 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 216:
#line 2246 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "ivec2"; }
#line 5706 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 217:
#line 2247 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "ivec3"; }
#line 5712 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 218:
#line 2248 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "ivec4"; }
#line 5718 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 219:
#line 2249 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uvec2"; }
#line 5724 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 220:
#line 2250 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uvec3"; }
#line 5730 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 221:
#line 2251 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uvec4"; }
#line 5736 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 222:
#line 2252 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dvec2"; }
#line 5742 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 223:
#line 2253 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dvec3"; }
#line 5748 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 224:
#line 2254 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dvec4"; }
#line 5754 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 225:
#line 2255 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat2"; }
#line 5760 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 226:
#line 2256 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat2x3"; }
#line 5766 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 227:
#line 2257 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat2x4"; }
#line 5772 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 228:
#line 2258 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat3x2"; }
#line 5778 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 229:
#line 2259 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat3"; }
#line 5784 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 230:
#line 2260 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat3x4"; }
#line 5790 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 231:
#line 2261 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat4x2"; }
#line 5796 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 232:
#line 2262 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat4x3"; }
#line 5802 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 233:
#line 2263 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat4"; }
#line 5808 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 234:
#line 2264 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat2"; }
#line 5814 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 235:
#line 2265 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat2x3"; }
#line 5820 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 236:
#line 2266 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat2x4"; }
#line 5826 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 237:
#line 2267 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat3x2"; }
#line 5832 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 238:
#line 2268 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat3"; }
#line 5838 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 239:
#line 2269 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat3x4"; }
#line 5844 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 240:
#line 2270 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat4x2"; }
#line 5850 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 241:
#line 2271 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat4x3"; }
#line 5856 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 242:
#line 2272 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat4"; }
#line 5862 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 243:
#line 2273 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1D"; }
#line 5868 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 244:
#line 2274 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2D"; }
#line 5874 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 245:
#line 2275 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DRect"; }
#line 5880 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 246:
#line 2276 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler3D"; }
#line 5886 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 247:
#line 2277 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCube"; }
#line 5892 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 248:
#line 2278 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerExternalOES"; }
#line 5898 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 249:
#line 2279 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1DShadow"; }
#line 5904 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 250:
#line 2280 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DShadow"; }
#line 5910 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 251:
#line 2281 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DRectShadow"; }
#line 5916 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 252:
#line 2282 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCubeShadow"; }
#line 5922 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 253:
#line 2283 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1DArray"; }
#line 5928 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 254:
#line 2284 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DArray"; }
#line 5934 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 255:
#line 2285 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1DArrayShadow"; }
#line 5940 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 256:
#line 2286 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DArrayShadow"; }
#line 5946 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 257:
#line 2287 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerBuffer"; }
#line 5952 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 258:
#line 2288 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCubeArray"; }
#line 5958 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 259:
#line 2289 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCubeArrayShadow"; }
#line 5964 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 260:
#line 2290 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler1D"; }
#line 5970 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 261:
#line 2291 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2D"; }
#line 5976 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 262:
#line 2292 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DRect"; }
#line 5982 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 263:
#line 2293 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler3D"; }
#line 5988 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 264:
#line 2294 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isamplerCube"; }
#line 5994 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 265:
#line 2295 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler1DArray"; }
#line 6000 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 266:
#line 2296 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DArray"; }
#line 6006 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 267:
#line 2297 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isamplerBuffer"; }
#line 6012 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 268:
#line 2298 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isamplerCubeArray"; }
#line 6018 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 269:
#line 2299 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler1D"; }
#line 6024 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 270:
#line 2300 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2D"; }
#line 6030 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 271:
#line 2301 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DRect"; }
#line 6036 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 272:
#line 2302 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler3D"; }
#line 6042 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 273:
#line 2303 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usamplerCube"; }
#line 6048 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 274:
#line 2304 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler1DArray"; }
#line 6054 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 275:
#line 2305 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DArray"; }
#line 6060 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 276:
#line 2306 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usamplerBuffer"; }
#line 6066 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 277:
#line 2307 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usamplerCubeArray"; }
#line 6072 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 278:
#line 2308 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DMS"; }
#line 6078 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 279:
#line 2309 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DMS"; }
#line 6084 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 280:
#line 2310 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DMS"; }
#line 6090 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 281:
#line 2311 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DMSArray"; }
#line 6096 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 282:
#line 2312 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DMSArray"; }
#line 6102 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 283:
#line 2313 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DMSArray"; }
#line 6108 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 284:
#line 2314 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image1D"; }
#line 6114 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 285:
#line 2315 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2D"; }
#line 6120 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 286:
#line 2316 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image3D"; }
#line 6126 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 287:
#line 2317 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DRect"; }
#line 6132 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 288:
#line 2318 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "imageCube"; }
#line 6138 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 289:
#line 2319 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "imageBuffer"; }
#line 6144 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 290:
#line 2320 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image1DArray"; }
#line 6150 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 291:
#line 2321 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DArray"; }
#line 6156 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 292:
#line 2322 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "imageCubeArray"; }
#line 6162 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 293:
#line 2323 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DMS"; }
#line 6168 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 294:
#line 2324 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DMSArray"; }
#line 6174 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 295:
#line 2325 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage1D"; }
#line 6180 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 296:
#line 2326 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2D"; }
#line 6186 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 297:
#line 2327 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage3D"; }
#line 6192 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 298:
#line 2328 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DRect"; }
#line 6198 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 299:
#line 2329 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimageCube"; }
#line 6204 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 300:
#line 2330 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimageBuffer"; }
#line 6210 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 301:
#line 2331 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage1DArray"; }
#line 6216 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 302:
#line 2332 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DArray"; }
#line 6222 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 303:
#line 2333 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimageCubeArray"; }
#line 6228 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 304:
#line 2334 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DMS"; }
#line 6234 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 305:
#line 2335 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DMSArray"; }
#line 6240 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 306:
#line 2336 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage1D"; }
#line 6246 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 307:
#line 2337 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2D"; }
#line 6252 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 308:
#line 2338 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage3D"; }
#line 6258 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 309:
#line 2339 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DRect"; }
#line 6264 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 310:
#line 2340 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimageCube"; }
#line 6270 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 311:
#line 2341 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimageBuffer"; }
#line 6276 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 312:
#line 2342 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage1DArray"; }
#line 6282 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 313:
#line 2343 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DArray"; }
#line 6288 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 314:
#line 2344 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimageCubeArray"; }
#line 6294 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 315:
#line 2345 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DMS"; }
#line 6300 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 316:
#line 2346 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DMSArray"; }
#line 6306 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 317:
#line 2347 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "atomic_uint"; }
#line 6312 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 318:
#line 2348 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "int64_t";	   }
#line 6318 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 319:
#line 2349 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "i64vec2";	   }
#line 6324 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 320:
#line 2350 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "i64vec3";	   }
#line 6330 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 321:
#line 2351 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "i64vec4";	   }
#line 6336 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 322:
#line 2352 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uint64_t";	   }
#line 6342 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 323:
#line 2353 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "u64vec2";	   }
#line 6348 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 324:
#line 2354 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "u64vec3";	   }
#line 6354 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 325:
#line 2355 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "u64vec4";	   }
#line 6360 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 326:
#line 2360 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_high;
   }
#line 6369 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 327:
#line 2365 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_medium;
   }
#line 6378 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 328:
#line 2370 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_low;
   }
#line 6387 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 329:
#line 2378 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.struct_specifier) = new(ctx) ast_struct_specifier(ctx, (yyvsp[-3].identifier), (yyvsp[-1].declarator_list));
      (yyval.struct_specifier)->set_location_range((yylsp[-3]), (yylsp[0]));
      state->symbols->add_type((yyvsp[-3].identifier), glsl_type::void_type);
   }
#line 6398 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 330:
#line 2385 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.struct_specifier) = new(ctx) ast_struct_specifier(ctx, NULL, (yyvsp[-1].declarator_list));
      (yyval.struct_specifier)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 6408 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 331:
#line 2394 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[0].declarator_list);
      (yyvsp[0].declarator_list)->link.self_link();
   }
#line 6417 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 332:
#line 2399 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[-1].declarator_list);
      (yyval.declarator_list)->link.insert_before(& (yyvsp[0].declarator_list)->link);
   }
#line 6426 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 333:
#line 2407 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 6465 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 334:
#line 2445 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declaration) = (yyvsp[0].declaration);
      (yyvsp[0].declaration)->link.self_link();
   }
#line 6474 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 335:
#line 2450 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declaration) = (yyvsp[-2].declaration);
      (yyval.declaration)->link.insert_before(& (yyvsp[0].declaration)->link);
   }
#line 6483 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 336:
#line 2458 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.declaration) = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      (yyval.declaration)->set_location((yylsp[0]));
   }
#line 6493 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 337:
#line 2464 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.declaration) = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      (yyval.declaration)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6503 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 339:
#line 2474 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
   }
#line 6511 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 340:
#line 2478 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-2].expression);
   }
#line 6519 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 341:
#line 2485 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.expression) = new(ctx) ast_aggregate_initializer();
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 6530 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 342:
#line 2492 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyvsp[-2].expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 6538 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 344:
#line 2504 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (ast_node *) (yyvsp[0].compound_statement); }
#line 6544 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 352:
#line 2519 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(true, NULL);
      (yyval.compound_statement)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6554 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 353:
#line 2525 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->symbols->push_scope();
   }
#line 6562 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 354:
#line 2529 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(true, (yyvsp[-1].node));
      (yyval.compound_statement)->set_location_range((yylsp[-3]), (yylsp[0]));
      state->symbols->pop_scope();
   }
#line 6573 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 355:
#line 2538 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (ast_node *) (yyvsp[0].compound_statement); }
#line 6579 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 357:
#line 2544 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(false, NULL);
      (yyval.compound_statement)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6589 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 358:
#line 2550 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(false, (yyvsp[-1].node));
      (yyval.compound_statement)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 6599 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 359:
#line 2559 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].node) == NULL) {
         _mesa_glsl_error(& (yylsp[0]), state, "<nil> statement");
         assert((yyvsp[0].node) != NULL);
      }

      (yyval.node) = (yyvsp[0].node);
      (yyval.node)->link.self_link();
   }
#line 6613 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 360:
#line 2569 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].node) == NULL) {
         _mesa_glsl_error(& (yylsp[0]), state, "<nil> statement");
         assert((yyvsp[0].node) != NULL);
      }
      (yyval.node) = (yyvsp[-1].node);
      (yyval.node)->link.insert_before(& (yyvsp[0].node)->link);
   }
#line 6626 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 361:
#line 2581 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_expression_statement(NULL);
      (yyval.node)->set_location((yylsp[0]));
   }
#line 6636 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 362:
#line 2587 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_expression_statement((yyvsp[-1].expression));
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6646 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 363:
#line 2596 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = new(state->linalloc) ast_selection_statement((yyvsp[-2].expression), (yyvsp[0].selection_rest_statement).then_statement,
                                                        (yyvsp[0].selection_rest_statement).else_statement);
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 6656 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 364:
#line 2605 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.selection_rest_statement).then_statement = (yyvsp[-2].node);
      (yyval.selection_rest_statement).else_statement = (yyvsp[0].node);
   }
#line 6665 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 365:
#line 2610 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.selection_rest_statement).then_statement = (yyvsp[0].node);
      (yyval.selection_rest_statement).else_statement = NULL;
   }
#line 6674 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 366:
#line 2618 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (ast_node *) (yyvsp[0].expression);
   }
#line 6682 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 367:
#line 2622 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      ast_declarator_list *declarator = new(ctx) ast_declarator_list((yyvsp[-3].fully_specified_type));
      decl->set_location_range((yylsp[-2]), (yylsp[0]));
      declarator->set_location((yylsp[-3]));

      declarator->declarations.push_tail(&decl->link);
      (yyval.node) = declarator;
   }
#line 6697 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 368:
#line 2640 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = new(state->linalloc) ast_switch_statement((yyvsp[-2].expression), (yyvsp[0].switch_body));
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 6706 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 369:
#line 2648 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.switch_body) = new(state->linalloc) ast_switch_body(NULL);
      (yyval.switch_body)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6715 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 370:
#line 2653 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.switch_body) = new(state->linalloc) ast_switch_body((yyvsp[-1].case_statement_list));
      (yyval.switch_body)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 6724 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 371:
#line 2661 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label) = new(state->linalloc) ast_case_label((yyvsp[-1].expression));
      (yyval.case_label)->set_location((yylsp[-1]));
   }
#line 6733 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 372:
#line 2666 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label) = new(state->linalloc) ast_case_label(NULL);
      (yyval.case_label)->set_location((yylsp[0]));
   }
#line 6742 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 373:
#line 2674 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_label_list *labels = new(state->linalloc) ast_case_label_list();

      labels->labels.push_tail(& (yyvsp[0].case_label)->link);
      (yyval.case_label_list) = labels;
      (yyval.case_label_list)->set_location((yylsp[0]));
   }
#line 6754 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 374:
#line 2682 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label_list) = (yyvsp[-1].case_label_list);
      (yyval.case_label_list)->labels.push_tail(& (yyvsp[0].case_label)->link);
   }
#line 6763 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 375:
#line 2690 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_statement *stmts = new(state->linalloc) ast_case_statement((yyvsp[-1].case_label_list));
      stmts->set_location((yylsp[0]));

      stmts->stmts.push_tail(& (yyvsp[0].node)->link);
      (yyval.case_statement) = stmts;
   }
#line 6775 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 376:
#line 2698 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_statement) = (yyvsp[-1].case_statement);
      (yyval.case_statement)->stmts.push_tail(& (yyvsp[0].node)->link);
   }
#line 6784 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 377:
#line 2706 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_statement_list *cases= new(state->linalloc) ast_case_statement_list();
      cases->set_location((yylsp[0]));

      cases->cases.push_tail(& (yyvsp[0].case_statement)->link);
      (yyval.case_statement_list) = cases;
   }
#line 6796 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 378:
#line 2714 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_statement_list) = (yyvsp[-1].case_statement_list);
      (yyval.case_statement_list)->cases.push_tail(& (yyvsp[0].case_statement)->link);
   }
#line 6805 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 379:
#line 2722 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_while,
                                            NULL, (yyvsp[-2].node), NULL, (yyvsp[0].node));
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[-1]));
   }
#line 6816 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 380:
#line 2729 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_do_while,
                                            NULL, (yyvsp[-2].expression), NULL, (yyvsp[-5].node));
      (yyval.node)->set_location_range((yylsp[-6]), (yylsp[-1]));
   }
#line 6827 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 381:
#line 2736 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_for,
                                            (yyvsp[-3].node), (yyvsp[-2].for_rest_statement).cond, (yyvsp[-2].for_rest_statement).rest, (yyvsp[0].node));
      (yyval.node)->set_location_range((yylsp[-5]), (yylsp[0]));
   }
#line 6838 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 385:
#line 2752 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
   }
#line 6846 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 386:
#line 2759 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.for_rest_statement).cond = (yyvsp[-1].node);
      (yyval.for_rest_statement).rest = NULL;
   }
#line 6855 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 387:
#line 2764 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.for_rest_statement).cond = (yyvsp[-2].node);
      (yyval.for_rest_statement).rest = (yyvsp[0].expression);
   }
#line 6864 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 388:
#line 2773 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_continue, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6874 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 389:
#line 2779 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_break, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6884 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 390:
#line 2785 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_return, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6894 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 391:
#line 2791 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_return, (yyvsp[-1].expression));
      (yyval.node)->set_location_range((yylsp[-2]), (yylsp[-1]));
   }
#line 6904 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 392:
#line 2797 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_discard, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6914 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 393:
#line 2805 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].function_definition); }
#line 6920 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 394:
#line 2806 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].node); }
#line 6926 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 395:
#line 2807 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = NULL; }
#line 6932 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 396:
#line 2808 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].node); }
#line 6938 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 397:
#line 2813 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state->linalloc;
      (yyval.function_definition) = new(ctx) ast_function_definition();
      (yyval.function_definition)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.function_definition)->prototype = (yyvsp[-1].function);
      (yyval.function_definition)->body = (yyvsp[0].compound_statement);

      state->symbols->pop_scope();
   }
#line 6952 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 398:
#line 2827 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[0].interface_block);
   }
#line 6960 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 399:
#line 2831 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *block = (ast_interface_block *) (yyvsp[0].node);

      if (!(yyvsp[-1].type_qualifier).merge_qualifier(& (yylsp[-1]), state, block->layout, false,
                              block->layout.has_layout())) {
         YYERROR;
      }

      block->layout = (yyvsp[-1].type_qualifier);

      (yyval.node) = block;
   }
#line 6977 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 400:
#line 2844 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 6996 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 401:
#line 2862 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 7016 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 402:
#line 2881 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 7025 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 403:
#line 2886 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 7034 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 404:
#line 2891 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.uniform = 1;
   }
#line 7043 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 405:
#line 2896 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.buffer = 1;
   }
#line 7052 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 406:
#line 2901 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 7067 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 407:
#line 2915 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state->linalloc) ast_interface_block(NULL, NULL);
   }
#line 7075 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 408:
#line 2919 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state->linalloc) ast_interface_block((yyvsp[0].identifier), NULL);
      (yyval.interface_block)->set_location((yylsp[0]));
   }
#line 7084 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 409:
#line 2924 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state->linalloc) ast_interface_block((yyvsp[-1].identifier), (yyvsp[0].array_specifier));
      (yyval.interface_block)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 7093 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 410:
#line 2932 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[0].declarator_list);
      (yyvsp[0].declarator_list)->link.self_link();
   }
#line 7102 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 411:
#line 2937 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[-1].declarator_list);
      (yyvsp[0].declarator_list)->link.insert_before(& (yyval.declarator_list)->link);
   }
#line 7111 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 412:
#line 2945 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 7136 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 413:
#line 2969 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
   }
#line 7147 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 415:
#line 2980 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
   }
#line 7158 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 417:
#line 2991 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
      if (!(yyval.type_qualifier).validate_in_qualifier(& (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 7172 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 418:
#line 3001 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!(yyvsp[-2].type_qualifier).validate_in_qualifier(& (yylsp[-2]), state)) {
         YYERROR;
      }
   }
#line 7182 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 419:
#line 3010 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[-1]), state, (yyvsp[0].type_qualifier), false, true)) {
         YYERROR;
      }
      if (!(yyval.type_qualifier).validate_out_qualifier(& (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 7196 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 420:
#line 3020 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!(yyvsp[-2].type_qualifier).validate_out_qualifier(& (yylsp[-2]), state)) {
         YYERROR;
      }
   }
#line 7206 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 421:
#line 3029 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 7222 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 422:
#line 3041 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 7248 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 423:
#line 3063 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!(yyvsp[0].type_qualifier).merge_into_in_qualifier(& (yylsp[0]), state, (yyval.node))) {
         YYERROR;
      }
      if (!state->in_qualifier->push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }
   }
#line 7262 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 424:
#line 3073 "./glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!(yyvsp[0].type_qualifier).merge_into_out_qualifier(& (yylsp[0]), state, (yyval.node))) {
         YYERROR;
      }
      if (!state->out_qualifier->push_to_global(& (yylsp[0]), state)) {
         YYERROR;
      }
   }
#line 7276 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;


#line 7280 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
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
