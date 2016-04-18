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
#line 1 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:339  */

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
    CENTROID = 292,
    IN_TOK = 293,
    OUT_TOK = 294,
    INOUT_TOK = 295,
    UNIFORM = 296,
    VARYING = 297,
    SAMPLE = 298,
    NOPERSPECTIVE = 299,
    FLAT = 300,
    SMOOTH = 301,
    MAT2X2 = 302,
    MAT2X3 = 303,
    MAT2X4 = 304,
    MAT3X2 = 305,
    MAT3X3 = 306,
    MAT3X4 = 307,
    MAT4X2 = 308,
    MAT4X3 = 309,
    MAT4X4 = 310,
    DMAT2X2 = 311,
    DMAT2X3 = 312,
    DMAT2X4 = 313,
    DMAT3X2 = 314,
    DMAT3X3 = 315,
    DMAT3X4 = 316,
    DMAT4X2 = 317,
    DMAT4X3 = 318,
    DMAT4X4 = 319,
    SAMPLER1D = 320,
    SAMPLER2D = 321,
    SAMPLER3D = 322,
    SAMPLERCUBE = 323,
    SAMPLER1DSHADOW = 324,
    SAMPLER2DSHADOW = 325,
    SAMPLERCUBESHADOW = 326,
    SAMPLER1DARRAY = 327,
    SAMPLER2DARRAY = 328,
    SAMPLER1DARRAYSHADOW = 329,
    SAMPLER2DARRAYSHADOW = 330,
    SAMPLERCUBEARRAY = 331,
    SAMPLERCUBEARRAYSHADOW = 332,
    ISAMPLER1D = 333,
    ISAMPLER2D = 334,
    ISAMPLER3D = 335,
    ISAMPLERCUBE = 336,
    ISAMPLER1DARRAY = 337,
    ISAMPLER2DARRAY = 338,
    ISAMPLERCUBEARRAY = 339,
    USAMPLER1D = 340,
    USAMPLER2D = 341,
    USAMPLER3D = 342,
    USAMPLERCUBE = 343,
    USAMPLER1DARRAY = 344,
    USAMPLER2DARRAY = 345,
    USAMPLERCUBEARRAY = 346,
    SAMPLER2DRECT = 347,
    ISAMPLER2DRECT = 348,
    USAMPLER2DRECT = 349,
    SAMPLER2DRECTSHADOW = 350,
    SAMPLERBUFFER = 351,
    ISAMPLERBUFFER = 352,
    USAMPLERBUFFER = 353,
    SAMPLER2DMS = 354,
    ISAMPLER2DMS = 355,
    USAMPLER2DMS = 356,
    SAMPLER2DMSARRAY = 357,
    ISAMPLER2DMSARRAY = 358,
    USAMPLER2DMSARRAY = 359,
    SAMPLEREXTERNALOES = 360,
    IMAGE1D = 361,
    IMAGE2D = 362,
    IMAGE3D = 363,
    IMAGE2DRECT = 364,
    IMAGECUBE = 365,
    IMAGEBUFFER = 366,
    IMAGE1DARRAY = 367,
    IMAGE2DARRAY = 368,
    IMAGECUBEARRAY = 369,
    IMAGE2DMS = 370,
    IMAGE2DMSARRAY = 371,
    IIMAGE1D = 372,
    IIMAGE2D = 373,
    IIMAGE3D = 374,
    IIMAGE2DRECT = 375,
    IIMAGECUBE = 376,
    IIMAGEBUFFER = 377,
    IIMAGE1DARRAY = 378,
    IIMAGE2DARRAY = 379,
    IIMAGECUBEARRAY = 380,
    IIMAGE2DMS = 381,
    IIMAGE2DMSARRAY = 382,
    UIMAGE1D = 383,
    UIMAGE2D = 384,
    UIMAGE3D = 385,
    UIMAGE2DRECT = 386,
    UIMAGECUBE = 387,
    UIMAGEBUFFER = 388,
    UIMAGE1DARRAY = 389,
    UIMAGE2DARRAY = 390,
    UIMAGECUBEARRAY = 391,
    UIMAGE2DMS = 392,
    UIMAGE2DMSARRAY = 393,
    IMAGE1DSHADOW = 394,
    IMAGE2DSHADOW = 395,
    IMAGE1DARRAYSHADOW = 396,
    IMAGE2DARRAYSHADOW = 397,
    COHERENT = 398,
    VOLATILE = 399,
    RESTRICT = 400,
    READONLY = 401,
    WRITEONLY = 402,
    ATOMIC_UINT = 403,
    SHARED = 404,
    STRUCT = 405,
    VOID_TOK = 406,
    WHILE = 407,
    IDENTIFIER = 408,
    TYPE_IDENTIFIER = 409,
    NEW_IDENTIFIER = 410,
    FLOATCONSTANT = 411,
    DOUBLECONSTANT = 412,
    INTCONSTANT = 413,
    UINTCONSTANT = 414,
    BOOLCONSTANT = 415,
    FIELD_SELECTION = 416,
    LEFT_OP = 417,
    RIGHT_OP = 418,
    INC_OP = 419,
    DEC_OP = 420,
    LE_OP = 421,
    GE_OP = 422,
    EQ_OP = 423,
    NE_OP = 424,
    AND_OP = 425,
    OR_OP = 426,
    XOR_OP = 427,
    MUL_ASSIGN = 428,
    DIV_ASSIGN = 429,
    ADD_ASSIGN = 430,
    MOD_ASSIGN = 431,
    LEFT_ASSIGN = 432,
    RIGHT_ASSIGN = 433,
    AND_ASSIGN = 434,
    XOR_ASSIGN = 435,
    OR_ASSIGN = 436,
    SUB_ASSIGN = 437,
    INVARIANT = 438,
    PRECISE = 439,
    LOWP = 440,
    MEDIUMP = 441,
    HIGHP = 442,
    SUPERP = 443,
    PRECISION = 444,
    VERSION_TOK = 445,
    EXTENSION = 446,
    LINE = 447,
    COLON = 448,
    EOL = 449,
    INTERFACE = 450,
    OUTPUT = 451,
    PRAGMA_DEBUG_ON = 452,
    PRAGMA_DEBUG_OFF = 453,
    PRAGMA_OPTIMIZE_ON = 454,
    PRAGMA_OPTIMIZE_OFF = 455,
    PRAGMA_INVARIANT_ALL = 456,
    LAYOUT_TOK = 457,
    DOT_TOK = 458,
    ASM = 459,
    CLASS = 460,
    UNION = 461,
    ENUM = 462,
    TYPEDEF = 463,
    TEMPLATE = 464,
    THIS = 465,
    PACKED_TOK = 466,
    GOTO = 467,
    INLINE_TOK = 468,
    NOINLINE = 469,
    PUBLIC_TOK = 470,
    STATIC = 471,
    EXTERN = 472,
    EXTERNAL = 473,
    LONG_TOK = 474,
    SHORT_TOK = 475,
    HALF = 476,
    FIXED_TOK = 477,
    UNSIGNED = 478,
    INPUT_TOK = 479,
    HVEC2 = 480,
    HVEC3 = 481,
    HVEC4 = 482,
    FVEC2 = 483,
    FVEC3 = 484,
    FVEC4 = 485,
    SAMPLER3DRECT = 486,
    SIZEOF = 487,
    CAST = 488,
    NAMESPACE = 489,
    USING = 490,
    RESOURCE = 491,
    PATCH = 492,
    SUBROUTINE = 493,
    ERROR_TOK = 494,
    COMMON = 495,
    PARTITION = 496,
    ACTIVE = 497,
    FILTER = 498,
    ROW_MAJOR = 499,
    THEN = 500
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 98 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:355  */

   int n;
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

#line 476 "glsl/glsl_parser.cpp" /* yacc.c:355  */
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

#line 506 "glsl/glsl_parser.cpp" /* yacc.c:358  */

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
#define YYLAST   5754

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  269
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  112
/* YYNRULES -- Number of rules.  */
#define YYNRULES  416
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  577

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   500

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint16 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   253,     2,     2,     2,   257,   260,     2,
     246,   247,   255,   251,   250,   252,     2,   256,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,   264,   266,
     258,   265,   259,   263,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   248,     2,   249,   261,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   267,   262,   268,   254,     2,     2,     2,
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
     245
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   312,   312,   311,   335,   337,   344,   354,   355,   356,
     357,   358,   382,   384,   388,   389,   390,   394,   403,   411,
     419,   430,   431,   435,   442,   449,   456,   463,   470,   477,
     484,   485,   491,   495,   502,   508,   517,   521,   525,   529,
     530,   534,   535,   539,   545,   557,   561,   567,   581,   582,
     588,   594,   604,   605,   606,   607,   611,   612,   618,   624,
     633,   634,   640,   649,   650,   656,   665,   666,   672,   678,
     684,   693,   694,   700,   709,   710,   719,   720,   729,   730,
     739,   740,   749,   750,   759,   760,   769,   770,   779,   780,
     789,   790,   791,   792,   793,   794,   795,   796,   797,   798,
     799,   803,   807,   823,   827,   832,   836,   841,   848,   852,
     853,   857,   862,   870,   888,   898,   912,   917,   931,   934,
     942,   950,   962,   974,   981,   986,   991,  1000,  1004,  1005,
    1015,  1025,  1035,  1049,  1056,  1066,  1076,  1086,  1096,  1108,
    1123,  1130,  1145,  1152,  1153,  1163,  1458,  1580,  1606,  1611,
    1616,  1624,  1629,  1638,  1647,  1659,  1664,  1669,  1678,  1683,
    1688,  1689,  1690,  1691,  1692,  1693,  1694,  1712,  1720,  1743,
    1767,  1784,  1789,  1805,  1825,  1837,  1845,  1850,  1855,  1862,
    1867,  1872,  1877,  1882,  1901,  1906,  1911,  1919,  1924,  1929,
    1935,  1940,  1948,  1956,  1962,  1972,  1983,  1984,  1992,  1998,
    2004,  2013,  2014,  2015,  2016,  2017,  2018,  2019,  2020,  2021,
    2022,  2023,  2024,  2025,  2026,  2027,  2028,  2029,  2030,  2031,
    2032,  2033,  2034,  2035,  2036,  2037,  2038,  2039,  2040,  2041,
    2042,  2043,  2044,  2045,  2046,  2047,  2048,  2049,  2050,  2051,
    2052,  2053,  2054,  2055,  2056,  2057,  2058,  2059,  2060,  2061,
    2062,  2063,  2064,  2065,  2066,  2067,  2068,  2069,  2070,  2071,
    2072,  2073,  2074,  2075,  2076,  2077,  2078,  2079,  2080,  2081,
    2082,  2083,  2084,  2085,  2086,  2087,  2088,  2089,  2090,  2091,
    2092,  2093,  2094,  2095,  2096,  2097,  2098,  2099,  2100,  2101,
    2102,  2103,  2104,  2105,  2106,  2107,  2108,  2109,  2110,  2111,
    2112,  2113,  2114,  2115,  2116,  2117,  2118,  2119,  2120,  2121,
    2122,  2123,  2124,  2125,  2126,  2130,  2135,  2140,  2148,  2155,
    2164,  2169,  2177,  2196,  2201,  2209,  2215,  2224,  2225,  2229,
    2236,  2243,  2250,  2256,  2257,  2261,  2262,  2263,  2264,  2265,
    2266,  2270,  2277,  2276,  2290,  2291,  2295,  2301,  2310,  2320,
    2332,  2338,  2347,  2356,  2361,  2369,  2373,  2391,  2399,  2404,
    2412,  2417,  2425,  2433,  2441,  2449,  2457,  2465,  2473,  2480,
    2487,  2497,  2498,  2502,  2504,  2510,  2515,  2524,  2530,  2536,
    2542,  2548,  2557,  2558,  2559,  2560,  2564,  2578,  2582,  2600,
    2617,  2628,  2642,  2647,  2652,  2660,  2669,  2673,  2679,  2689,
    2693,  2699,  2708,  2713,  2721,  2745,  2758,  2769,  2782,  2804,
    2817,  2828,  2841,  2851,  2852,  2853,  2854
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
  "DVEC4", "CENTROID", "IN_TOK", "OUT_TOK", "INOUT_TOK", "UNIFORM",
  "VARYING", "SAMPLE", "NOPERSPECTIVE", "FLAT", "SMOOTH", "MAT2X2",
  "MAT2X3", "MAT2X4", "MAT3X2", "MAT3X3", "MAT3X4", "MAT4X2", "MAT4X3",
  "MAT4X4", "DMAT2X2", "DMAT2X3", "DMAT2X4", "DMAT3X2", "DMAT3X3",
  "DMAT3X4", "DMAT4X2", "DMAT4X3", "DMAT4X4", "SAMPLER1D", "SAMPLER2D",
  "SAMPLER3D", "SAMPLERCUBE", "SAMPLER1DSHADOW", "SAMPLER2DSHADOW",
  "SAMPLERCUBESHADOW", "SAMPLER1DARRAY", "SAMPLER2DARRAY",
  "SAMPLER1DARRAYSHADOW", "SAMPLER2DARRAYSHADOW", "SAMPLERCUBEARRAY",
  "SAMPLERCUBEARRAYSHADOW", "ISAMPLER1D", "ISAMPLER2D", "ISAMPLER3D",
  "ISAMPLERCUBE", "ISAMPLER1DARRAY", "ISAMPLER2DARRAY",
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
  "UINTCONSTANT", "BOOLCONSTANT", "FIELD_SELECTION", "LEFT_OP", "RIGHT_OP",
  "INC_OP", "DEC_OP", "LE_OP", "GE_OP", "EQ_OP", "NE_OP", "AND_OP",
  "OR_OP", "XOR_OP", "MUL_ASSIGN", "DIV_ASSIGN", "ADD_ASSIGN",
  "MOD_ASSIGN", "LEFT_ASSIGN", "RIGHT_ASSIGN", "AND_ASSIGN", "XOR_ASSIGN",
  "OR_ASSIGN", "SUB_ASSIGN", "INVARIANT", "PRECISE", "LOWP", "MEDIUMP",
  "HIGHP", "SUPERP", "PRECISION", "VERSION_TOK", "EXTENSION", "LINE",
  "COLON", "EOL", "INTERFACE", "OUTPUT", "PRAGMA_DEBUG_ON",
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
  "basic_interface_block", "interface_qualifier",
  "buffer_interface_qualifier", "instance_name_opt",
  "buffer_instance_name_opt", "member_list", "member_declaration",
  "layout_uniform_defaults", "layout_buffer_defaults",
  "layout_in_defaults", "layout_out_defaults", "layout_defaults", YY_NULLPTR
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
     495,   496,   497,   498,   499,   500,    40,    41,    91,    93,
      44,    43,    45,    33,   126,    42,    47,    37,    60,    62,
      38,    94,   124,    63,    58,    61,    59,   123,   125
};
# endif

#define YYPACT_NINF -430

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-430)))

#define YYTABLE_NINF -396

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -163,  -127,    36,  -430,  -115,  -430,  -136,  -430,  -430,  -430,
    -430,  -121,    29,  4941,  -430,  -430,  -126,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,   -50,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,   -44,   -39,   -28,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -143,  -430,
    -430,   296,   296,  -430,  -430,  -430,    25,   -91,   -60,   -40,
     -31,   -25,   -73,  -430,   -58,  -430,  4724,  -430,   -66,  -103,
     -56,   102,  -185,  -430,    81,  5310,  5320,  5320,  5600,  5320,
    5320,  5380,  -430,   -34,  -430,  5320,  -430,  -430,  -430,  -430,
    -430,    61,    67,  -430,  -430,  -430,  -430,  -430,    29,  5158,
     -48,  -430,  -430,  -430,  -430,  -430,  -430,  5320,  5320,  -430,
    5320,  -430,  5320,  -430,  -430,  5600,  -430,  -430,  -430,  -430,
    -430,  -107,    29,  -430,  -430,  -430,   564,  -430,  -430,     5,
       5,  -430,  -430,  -430,     5,  -430,  5600,     5,     5,     5,
      29,  -430,   -22,   -15,  -189,    13,  -135,  -134,  -133,  -131,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  5380,  -430,  -430,  3867,    -5,  -430,    -1,     2,    63,
      29,  1348,  -430,  5158,     4,  -430,  -430,  -430,     6,  -125,
    -430,  -430,  -430,  -111,     8,    10,  2114,    26,    33,    15,
    3381,    45,    47,  -430,  -430,  -430,  -430,  -430,  4283,  4283,
    4283,  -430,  -430,  -430,  -430,  -430,    28,  -430,  -430,  -430,
    -101,  -430,  -430,  -430,    50,  -104,  4491,    48,   142,  4283,
     -18,   -45,    64,   -98,    72,    38,    40,    41,   132,   133,
    -146,  -430,  -430,  -180,  -430,    42,    60,  -430,  -430,  -430,
    -430,   830,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  -430,    29,  -430,  -430,  -430,  -188,  3131,  -182,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,    76,  -430,  4075,
    5158,  5158,  -430,   -34,  -176,  -430,  -430,  -430,  1595,  -430,
    4283,  -430,  -107,  -430,    29,  -430,  -430,   158,  2631,  4283,
    -430,  -430,  -175,  4283,  3633,  -430,  -430,   -69,  -430,  2114,
    -430,  -430,   151,  4283,  -430,  -430,  4283,    66,  -430,  -430,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    -430,  4283,  -430,  4283,  4283,  4283,  4283,  4283,  4283,  4283,
    4283,  4283,  4283,  4283,  4283,  4283,  4283,  4283,  4283,  4283,
    4283,  4283,  4283,  4283,  -430,  -430,  -430,   -34,  3131,  -177,
    3131,  -430,  -430,  3131,  -430,  -430,    77,    29,    59,  5158,
      75,    -5,    29,  -430,  -430,  -430,  -430,  -430,    83,  -430,
    -430,  3633,   -54,  -430,   -52,    94,    29,    84,  -430,  1096,
    -430,    96,    94,  -430,  -430,  -430,  -430,  -430,   -18,   -18,
     -45,   -45,    64,    64,    64,    64,   -98,   -98,    72,    38,
      40,    41,   132,   133,  -157,  -430,    -5,  -430,  3131,  -430,
    -217,  -430,  -430,  -172,   191,  -430,   192,  -430,  4283,  -430,
      82,   103,  2114,    85,    88,  2379,  -430,  -430,  4283,  -430,
    2881,  -430,  -430,   -34,    89,   -34,    90,   -42,  4283,  2379,
     340,  -430,    -7,  -430,  3131,  -430,  -430,  -430,  -430,  -430,
    -430,    -5,  -430,    -5,  -430,    93,    94,  -430,  2114,  4283,
      97,  -430,  -430,  1849,  2114,    -3,  -430,  -430,  -430,  -155,
    -430,  -430,  -430,  -430,  -430,  2114,  -430
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       4,     0,     0,    12,     0,     1,     2,    14,    15,    16,
       5,     0,     0,     0,    13,     6,     0,   180,   179,   206,
     202,   204,   205,   203,   185,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   207,   208,   209,   219,   220,   221,
     176,   182,   183,   184,   181,   177,   157,   156,   155,   222,
     223,   224,   225,   226,   227,   228,   229,   230,   231,   232,
     233,   234,   235,   236,   237,   238,   239,   240,   241,   243,
     244,   246,   247,   249,   250,   251,   252,   253,   255,   256,
     257,   258,   260,   261,   262,   263,   265,   266,   267,   269,
     270,   271,   272,   274,   242,   259,   268,   248,   254,   264,
     273,   275,   276,   277,   278,   279,   280,   245,   281,   282,
     283,   284,   285,   286,   287,   288,   289,   290,   291,   292,
     293,   294,   295,   296,   297,   298,   299,   300,   301,   302,
     303,   304,   305,   306,   307,   308,   309,   310,   311,   312,
     313,   187,   188,   189,   190,   191,   314,   186,     0,   201,
     200,   158,   159,   317,   316,   315,     0,     0,     0,     0,
       0,     0,     0,   178,   151,   384,     3,   383,     0,     0,
     110,   118,     0,   128,   133,   163,   165,   162,     0,   160,
     161,   164,   140,   196,   198,   166,   199,    18,   382,   107,
     387,     0,     0,   413,   414,   415,   416,   385,     0,     0,
       0,   185,   182,   183,   184,    21,    22,   158,   159,   138,
     163,   168,   164,   139,   167,     0,     7,     8,     9,    10,
      11,     0,     0,    20,    19,   104,     0,   386,   108,   118,
     118,   124,   125,   126,   118,   111,     0,   118,   118,   118,
       0,   105,    14,    16,   134,     0,   185,   182,   183,   184,
     170,   388,   405,   407,   409,   411,   171,   169,   141,   172,
     173,   163,   175,   389,     0,   197,   174,     0,     0,     0,
       0,     0,   320,     0,     0,   150,   149,   148,   145,     0,
     143,   147,   153,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    26,    27,    24,    25,    28,     0,     0,
       0,    52,    53,    54,    55,   350,   342,   346,    23,    30,
      48,    32,    37,    38,     0,     0,    42,     0,    56,     0,
      60,    63,    66,    71,    74,    76,    78,    80,    82,    84,
      86,    88,   101,     0,   332,     0,   140,   335,   348,   334,
     333,     0,   336,   337,   338,   339,   340,   112,   119,   120,
     116,   117,   127,   121,   123,   122,   129,     0,   135,   113,
     408,   410,   412,   406,   192,    56,   103,     0,    46,     0,
       0,     0,    17,   325,     0,   323,   319,   321,     0,   106,
       0,   142,     0,   152,     0,   378,   377,     0,     0,     0,
     381,   379,     0,     0,     0,    49,    50,     0,   341,     0,
      34,    35,     0,     0,    40,    39,     0,   201,    43,    45,
      91,    92,    94,    93,    96,    97,    98,    99,   100,    95,
      90,     0,    51,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   351,   347,   349,   114,     0,   130,
       0,   327,   137,     0,   193,   194,     0,     0,     0,   402,
       0,   326,     0,   322,   318,   146,   144,   154,     0,   372,
     371,   374,     0,   380,     0,   355,     0,     0,    29,     0,
      33,     0,    36,    44,    89,    57,    58,    59,    61,    62,
      64,    65,    69,    70,    67,    68,    72,    73,    75,    77,
      79,    81,    83,    85,     0,   102,   115,   132,     0,   330,
       0,   136,   195,     0,   396,   403,   399,   324,     0,   373,
       0,     0,     0,     0,     0,     0,   343,    31,     0,   131,
       0,   328,   404,   397,     0,   400,     0,     0,   375,     0,
     354,   352,     0,   357,     0,   345,   368,   344,    87,   329,
     331,   398,   390,   401,   391,     0,   376,   370,     0,     0,
       0,   358,   362,     0,   366,     0,   356,   369,   353,     0,
     361,   364,   363,   365,   359,   367,   360
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -430,  -430,  -430,  -430,  -430,  -430,    11,   194,  -430,  -102,
    -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,  -430,
    5163,  -430,  -173,  -166,  -332,  -179,   -74,   -71,   -70,   -75,
     -68,   -67,  -430,  -257,  -139,  -430,  -274,  -272,   -10,    -8,
    -430,  -430,  -430,  -430,   138,   -63,  -430,  -430,  -430,  -430,
    -191,   -11,  -430,   -12,  -430,  -430,  -430,  -430,  -123,  -430,
    -430,    -9,  -243,   -13,  -430,  -430,   180,  -430,    98,  -252,
     -84,   -86,  -416,  -430,    -6,  -280,  -429,  -430,  -430,  -161,
     212,   -16,    -4,  -430,  -430,   -79,  -430,  -430,  -178,  -430,
    -171,  -430,  -430,  -430,  -430,  -430,  -430,   221,  -430,  -140,
    -430,  -430,  -430,  -430,  -430,  -341,  -430,   222,   223,   224,
     225,  -430
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     2,    13,     3,   165,     6,   373,    14,   166,   308,
     309,   310,   481,   311,   312,   313,   314,   315,   316,   317,
     318,   319,   320,   321,   322,   323,   324,   325,   326,   327,
     328,   329,   330,   331,   332,   421,   333,   367,   334,   335,
     169,   170,   171,   350,   235,   236,   237,   351,   172,   173,
     174,   210,   279,   280,   281,   176,   283,   177,   178,   179,
     180,   212,   265,   368,   183,   184,   185,   186,   271,   272,
     374,   375,   452,   510,   337,   338,   339,   340,   399,   546,
     547,   341,   342,   343,   541,   477,   344,   543,   562,   563,
     564,   565,   345,   471,   520,   521,   346,   187,   188,   189,
     190,   191,   192,   534,   536,   458,   459,   193,   194,   195,
     196,   197
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     182,   358,   175,   167,   181,   168,   387,   366,   270,   230,
       7,     8,     9,   559,   560,    11,   392,   559,   560,   377,
    -395,  -392,  -393,    16,  -394,   441,   397,     1,   211,   214,
     460,     4,   507,   530,   509,   251,     5,   511,     7,     8,
       9,   263,   275,   231,   232,   233,     7,     8,     9,   209,
     213,   531,   250,   256,   257,    12,   259,   260,   262,   264,
     264,   446,   266,   400,   401,   240,   369,   198,   430,   431,
     443,   369,   245,    15,   462,   443,   357,   448,   462,    10,
     270,   241,   270,   453,   211,   214,   444,   250,   508,   262,
     463,   473,   529,   443,   532,   443,   545,   456,   492,   493,
     494,   495,   402,   216,   276,  -395,   230,   528,   465,   576,
     545,  -392,   366,   449,   550,   472,  -393,   442,   515,   474,
     475,   251,   381,   366,   199,   382,   377,  -394,   566,   482,
     461,   360,   361,   362,   217,   363,   383,   277,   250,   384,
     231,   232,   233,   405,   228,   -47,   406,   403,   141,   142,
     143,   144,   145,   182,   218,   175,   167,   181,   168,   200,
     432,   433,   238,   219,   175,   258,   181,   348,   504,   220,
     261,   349,   181,   221,   353,   354,   355,   408,   478,   457,
     457,   443,     7,     8,     9,   244,   182,   270,   222,   234,
     153,   154,   155,   522,   229,   523,   443,   475,   443,   446,
     225,   226,   274,   476,   506,   555,   426,   427,   443,   269,
     153,   154,   155,   336,   264,   261,   267,   181,   451,   273,
     238,   238,   268,   352,   -21,   238,   428,   429,   238,   238,
     238,   -22,   278,   282,   242,     8,   243,   423,   424,   425,
     434,   435,   540,   369,   537,   141,   142,   143,   144,   145,
     261,   356,   181,   488,   489,   496,   497,   372,   182,   359,
     182,   561,   490,   491,   556,   574,   370,   483,   457,   371,
     379,   380,   388,   336,   385,   261,   386,   181,   568,   389,
     476,   390,   484,   571,   573,   569,   234,   153,   154,   155,
     551,   393,   553,   394,   409,   573,   398,   404,   436,    17,
      18,   437,   439,   438,   505,   440,   -46,   201,   225,   451,
     468,   451,   480,   -41,   451,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   454,   512,   514,   336,   518,
     261,   525,   181,    40,   202,   203,   215,   204,    44,    45,
      46,    47,    48,   516,   443,   527,   533,   535,   538,  -109,
     539,   239,   542,   544,   558,   552,   554,   182,   182,   567,
     223,   570,   498,   447,   501,   182,   499,   347,   500,   451,
     466,   378,   502,   513,   503,   336,   517,   261,   557,   181,
     227,   336,   469,   479,   470,   572,   336,   224,   261,   548,
     181,   451,   519,   278,   575,   467,     0,   252,   253,   254,
     255,     0,     0,     0,     0,   451,     0,   420,     0,   239,
     239,     0,     0,     0,   239,     0,     0,   239,   239,   239,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   141,
     142,   143,   144,   145,     0,   147,   182,     0,     0,   205,
       0,   206,     0,     0,     0,     0,     0,     0,   336,     0,
       0,     0,     0,     0,     0,     0,   336,     0,   261,     0,
     181,     0,     0,     0,     0,     0,     0,     0,     0,   207,
     208,   153,   154,   155,     0,     0,     0,   524,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   162,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   336,
       0,   261,   336,   181,   261,     0,   181,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   336,     0,   261,     0,
     181,     0,     0,   163,   164,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   336,     0,   261,     0,   181,
     336,   336,   261,   261,   181,   181,     0,     0,     0,     0,
       0,     0,   336,     0,   261,     0,   181,    17,    18,    19,
      20,    21,    22,    23,   284,    24,   285,   286,     0,   287,
     288,   289,   290,   291,     0,     0,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,     0,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   292,   205,   150,   206,
     293,   294,   295,   296,   297,     0,     0,     0,   298,   299,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   151,   152,   153,
     154,   155,     0,   156,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   162,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   163,   164,     0,     0,     0,     0,     0,     0,     0,
     300,     0,     0,     0,     0,   301,   302,   303,   304,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     305,   306,   307,    17,    18,    19,    20,    21,    22,    23,
     284,    24,   285,   286,     0,   287,   288,   289,   290,   291,
       0,     0,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
       0,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,     0,
       0,     0,     0,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   292,   205,   150,   206,   293,   294,   295,   296,
     297,     0,     0,     0,   298,   299,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   151,   152,   153,   154,   155,     0,   156,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   162,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   163,   164,     0,
       0,     0,     0,     0,     0,     0,   300,     0,     0,     0,
       0,   301,   302,   303,   304,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   305,   306,   445,    17,
      18,    19,    20,    21,    22,    23,   284,    24,   285,   286,
       0,   287,   288,   289,   290,   291,     0,     0,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,     0,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,     0,     0,     0,     0,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   292,   205,
     150,   206,   293,   294,   295,   296,   297,     0,     0,     0,
     298,   299,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   151,
     152,   153,   154,   155,     0,   156,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   162,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   163,   164,     0,     0,     0,     0,     0,
       0,     0,   300,     0,     0,     0,     0,   301,   302,   303,
     304,    17,    18,    19,    20,    21,    22,    23,     0,   201,
       0,     0,   305,   306,   526,     0,     0,     0,     0,     0,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,   202,   203,     0,   204,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,     0,     0,     0,
       0,   141,   142,   143,   144,   145,   146,   147,   148,   149,
       0,     0,   150,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   207,   208,   153,   154,   155,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     162,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   163,   164,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    17,    18,
      19,    20,    21,    22,    23,     0,   201,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   376,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,   202,   203,     0,   204,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,     0,     0,     0,     0,   141,   142,
     143,   144,   145,   146,   147,   148,   149,     0,     0,   150,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   207,   208,
     153,   154,   155,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   162,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   163,   164,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    17,    18,    19,    20,    21,    22,    23,   284,
      24,   285,   286,   464,   287,   288,   289,   290,   291,   559,
     560,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,     0,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,     0,     0,
       0,     0,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   292,   205,   150,   206,   293,   294,   295,   296,   297,
       0,     0,     0,   298,   299,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   151,   152,   153,   154,   155,     0,   156,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   162,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   163,   164,     0,     0,
       0,     0,     0,     0,     0,   300,     0,     0,     0,     0,
     301,   302,   303,   304,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   305,   306,    17,    18,    19,
      20,    21,    22,    23,   284,    24,   285,   286,     0,   287,
     288,   289,   290,   291,     0,     0,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,     0,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   292,   205,   150,   206,
     293,   294,   295,   296,   297,     0,     0,     0,   298,   299,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   151,   152,   153,
     154,   155,     0,   156,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   162,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   163,   164,     0,     0,     0,     0,     0,     0,     0,
     300,     0,     0,     0,     0,   301,   302,   303,   304,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     305,   306,    17,    18,    19,    20,    21,    22,    23,   284,
      24,   285,   286,     0,   287,   288,   289,   290,   291,     0,
       0,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,     0,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,     0,     0,
       0,     0,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   292,   205,   150,   206,   293,   294,   295,   296,   297,
       0,     0,     0,   298,   299,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   151,   152,   153,   154,   155,     0,   156,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   162,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   163,   164,     0,     0,
       0,     0,     0,     0,     0,   300,     0,     0,     0,     0,
     301,   302,   303,   304,    17,    18,    19,    20,    21,    22,
      23,     0,    24,     0,     0,   305,   226,     0,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,     0,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,   141,   142,   143,   144,   145,   146,
     147,   148,   149,     0,   205,   150,   206,   293,   294,   295,
     296,   297,     0,     0,     0,   298,   299,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   151,   152,   153,   154,   155,     0,
     156,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   162,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   163,   164,
       0,     0,     0,     0,     0,     0,     0,   300,     0,     0,
       0,     0,   301,   302,   303,   304,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,   305,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   146,
       0,   148,   149,     0,   205,   150,   206,   293,   294,   295,
     296,   297,     0,     0,     0,   298,   299,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   300,     0,     0,
       0,     0,   301,   302,   303,   304,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,     0,   450,   549,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   146,
       0,   148,   149,     0,   205,   150,   206,   293,   294,   295,
     296,   297,     0,     0,     0,   298,   299,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   300,     0,     0,
       0,     0,   301,   302,   303,   304,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,     0,   450,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   146,
       0,   148,   149,     0,   205,   150,   206,   293,   294,   295,
     296,   297,     0,     0,     0,   298,   299,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   300,     0,     0,
       0,     0,   301,   302,   303,   304,    17,    18,    19,    20,
      21,    22,    23,     0,   201,     0,     0,   391,     0,     0,
       0,     0,     0,     0,     0,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,   202,   203,     0,   204,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,     0,     0,     0,     0,   141,   142,   143,   144,
     145,   146,   147,   148,   149,     0,   205,   150,   206,   293,
     294,   295,   296,   297,     0,     0,     0,   298,   299,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   207,   208,   153,   154,
     155,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   162,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     163,   164,    19,    20,    21,    22,    23,     0,     0,   300,
       0,     0,     0,     0,   301,   302,   303,   304,     0,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   146,     0,   148,   149,     0,
     205,   150,   206,   293,   294,   295,   296,   297,     0,     0,
       0,   298,   299,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      19,    20,    21,    22,    23,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,     0,   300,     0,     0,   364,     0,   301,   302,
     303,   304,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   146,     0,   148,   149,     0,   205,   150,
     206,   293,   294,   295,   296,   297,     0,     0,     0,   298,
     299,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    19,    20,
      21,    22,    23,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
       0,   300,     0,     0,   455,     0,   301,   302,   303,   304,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   146,     0,   148,   149,     0,   205,   150,   206,   293,
     294,   295,   296,   297,     0,     0,     0,   298,   299,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    19,    20,    21,    22,
      23,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,     0,   300,
       0,     0,     0,     0,   301,   302,   303,   304,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   146,
       0,   148,   407,     0,   205,   150,   206,   293,   294,   295,
     296,   297,     0,     0,     0,   298,   299,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    17,    18,    19,
      20,    21,    22,    23,     0,    24,     0,   300,     0,     0,
       0,     0,   301,   302,   303,   304,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,     0,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,   141,   142,   143,
     144,   145,   146,   147,   148,   149,     0,     0,   150,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   151,   152,   153,
     154,   155,     0,   156,     0,    12,     0,     0,     0,     0,
       0,   157,   158,   159,   160,   161,   162,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    17,    18,    19,    20,    21,    22,
      23,     0,    24,     0,     0,     0,     0,     0,     0,     0,
       0,   163,   164,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,     0,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,   141,   142,   143,   144,   145,   146,
     147,   148,   149,     0,     0,   150,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   151,   152,   153,   154,   155,     0,
     156,     0,     0,     0,     0,     0,     0,     0,   157,   158,
     159,   160,   161,   162,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    17,    18,    19,    20,    21,    22,    23,     0,   201,
       0,     0,     0,     0,     0,     0,     0,     0,   163,   164,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,   202,   203,     0,   204,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,     0,     0,     0,
       0,   141,   142,   143,   144,   145,   146,   147,   148,   149,
       0,     0,   150,    17,    18,     0,     0,     0,     0,     0,
       0,   246,     0,    17,    18,     0,     0,     0,     0,     0,
       0,   201,     0,     0,     0,     0,     0,     0,     0,     0,
       0,   207,   208,   153,   154,   155,     0,    40,   247,   248,
       0,   249,    44,    45,    46,    47,    48,    40,   202,   203,
     162,   204,    44,    45,    46,    47,    48,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    17,    18,     0,     0,     0,     0,     0,
       0,    24,     0,     0,     0,   163,   164,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    40,    41,    42,
       0,    43,    44,    45,    46,    47,    48,   365,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   141,   142,   143,   144,   145,     0,   147,
       0,   395,   396,   141,   142,   143,   144,   145,     0,   147,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   422,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   207,   208,   153,   154,   155,     0,     0,
       0,     0,     0,   207,   208,   153,   154,   155,     0,     0,
       0,     0,   162,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   162,   141,   142,   143,   144,   145,     0,   147,
       0,     0,   365,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   365,     0,     0,     0,   163,   164,     0,
       0,     0,     0,     0,     0,     0,     0,   163,   164,     0,
       0,     0,     0,   207,   208,   153,   154,   155,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   162,     0,     0,     0,   485,   486,   487,   365,
     365,   365,   365,   365,   365,   365,   365,   365,   365,   365,
     365,   365,   365,   365,   365,    19,    20,    21,    22,    23,
       0,     0,     0,     0,     0,     0,     0,   163,   164,     0,
       0,     0,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   146,     0,
     148,   149,     0,     0,   150
};

static const yytype_int16 yycheck[] =
{
      13,   244,    13,    13,    13,    13,   286,   264,   199,     4,
     153,   154,   155,    20,    21,     4,   290,    20,    21,   271,
     155,   155,   155,    12,   155,   171,   300,   190,   151,   152,
     371,   158,   448,   250,   450,   175,     0,   453,   153,   154,
     155,   181,   149,    38,    39,    40,   153,   154,   155,   151,
     152,   268,   175,   176,   177,   191,   179,   180,   181,   248,
     248,   341,   185,   164,   165,   250,   248,   193,   166,   167,
     250,   248,   174,   194,   250,   250,   265,   265,   250,   194,
     271,   266,   273,   265,   207,   208,   266,   210,   265,   212,
     266,   266,   508,   250,   266,   250,   525,   369,   430,   431,
     432,   433,   203,   194,   211,   155,     4,   264,   380,   264,
     539,   155,   369,   356,   530,   389,   155,   263,   459,   393,
     394,   261,   247,   380,   267,   250,   378,   155,   544,   403,
     373,   266,   266,   266,   194,   266,   247,   244,   261,   250,
      38,    39,    40,   247,   247,   246,   250,   248,   143,   144,
     145,   146,   147,   166,   194,   166,   166,   166,   166,   148,
     258,   259,   171,   194,   175,   178,   175,   230,   442,   194,
     181,   234,   181,   246,   237,   238,   239,   316,   247,   370,
     371,   250,   153,   154,   155,   174,   199,   378,   246,   184,
     185,   186,   187,   247,   250,   247,   250,   471,   250,   479,
     266,   267,   215,   394,   447,   247,   251,   252,   250,   198,
     185,   186,   187,   226,   248,   226,   155,   226,   357,   267,
     229,   230,   155,   236,   246,   234,   162,   163,   237,   238,
     239,   246,   221,   222,   153,   154,   155,   255,   256,   257,
     168,   169,   522,   248,   518,   143,   144,   145,   146,   147,
     261,   240,   261,   426,   427,   434,   435,   194,   271,   246,
     273,   268,   428,   429,   538,   268,   267,   406,   459,   267,
     266,   265,   246,   286,   266,   286,   266,   286,   558,   246,
     471,   266,   421,   563,   564,   559,   184,   185,   186,   187,
     533,   246,   535,   246,   246,   575,   268,   247,   260,     3,
       4,   261,   170,   262,   443,   172,   246,    11,   266,   448,
     152,   450,   161,   247,   453,   173,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   249,   249,   268,   341,   246,
     341,   247,   341,    37,    38,    39,   156,    41,    42,    43,
      44,    45,    46,   268,   250,   249,   155,   155,   266,   247,
     247,   171,   267,   265,    14,   266,   266,   370,   371,   266,
     166,   264,   436,   352,   439,   378,   437,   229,   438,   508,
     382,   273,   440,   457,   441,   388,   462,   388,   539,   388,
     168,   394,   388,   399,   388,   563,   399,   166,   399,   528,
     399,   530,   471,   382,   565,   384,    -1,   175,   175,   175,
     175,    -1,    -1,    -1,    -1,   544,    -1,   265,    -1,   229,
     230,    -1,    -1,    -1,   234,    -1,    -1,   237,   238,   239,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   143,
     144,   145,   146,   147,    -1,   149,   459,    -1,    -1,   153,
      -1,   155,    -1,    -1,    -1,    -1,    -1,    -1,   471,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   479,    -1,   479,    -1,
     479,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   183,
     184,   185,   186,   187,    -1,    -1,    -1,   476,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   202,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   522,
      -1,   522,   525,   522,   525,    -1,   525,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   539,    -1,   539,    -1,
     539,    -1,    -1,   237,   238,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   558,    -1,   558,    -1,   558,
     563,   564,   563,   564,   563,   564,    -1,    -1,    -1,    -1,
      -1,    -1,   575,    -1,   575,    -1,   575,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    -1,    15,
      16,    17,    18,    19,    -1,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    -1,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,    -1,    -1,    -1,    -1,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   158,   159,   160,    -1,    -1,    -1,   164,   165,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   183,   184,   185,
     186,   187,    -1,   189,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   202,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   237,   238,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     246,    -1,    -1,    -1,    -1,   251,   252,   253,   254,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     266,   267,   268,     3,     4,     5,     6,     7,     8,     9,
      10,    11,    12,    13,    -1,    15,    16,    17,    18,    19,
      -1,    -1,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      -1,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,    -1,
      -1,    -1,    -1,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,    -1,    -1,    -1,   164,   165,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   183,   184,   185,   186,   187,    -1,   189,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   202,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   237,   238,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,    -1,
      -1,   251,   252,   253,   254,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   266,   267,   268,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      -1,    15,    16,    17,    18,    19,    -1,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    -1,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,    -1,    -1,    -1,    -1,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   158,   159,   160,    -1,    -1,    -1,
     164,   165,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   183,
     184,   185,   186,   187,    -1,   189,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   202,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   237,   238,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   246,    -1,    -1,    -1,    -1,   251,   252,   253,
     254,     3,     4,     5,     6,     7,     8,     9,    -1,    11,
      -1,    -1,   266,   267,   268,    -1,    -1,    -1,    -1,    -1,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    -1,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,    -1,    -1,    -1,
      -1,   143,   144,   145,   146,   147,   148,   149,   150,   151,
      -1,    -1,   154,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   183,   184,   185,   186,   187,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     202,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   237,   238,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,     4,
       5,     6,     7,     8,     9,    -1,    11,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   268,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    -1,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,    -1,    -1,    -1,    -1,   143,   144,
     145,   146,   147,   148,   149,   150,   151,    -1,    -1,   154,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   183,   184,
     185,   186,   187,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   202,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   237,   238,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,   268,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    -1,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,    -1,    -1,
      -1,    -1,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
      -1,    -1,    -1,   164,   165,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   183,   184,   185,   186,   187,    -1,   189,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   202,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   237,   238,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,    -1,    -1,
     251,   252,   253,   254,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   266,   267,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    -1,    15,
      16,    17,    18,    19,    -1,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    -1,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,    -1,    -1,    -1,    -1,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   158,   159,   160,    -1,    -1,    -1,   164,   165,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   183,   184,   185,
     186,   187,    -1,   189,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   202,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   237,   238,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     246,    -1,    -1,    -1,    -1,   251,   252,   253,   254,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     266,   267,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    -1,    15,    16,    17,    18,    19,    -1,
      -1,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    -1,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,    -1,    -1,
      -1,    -1,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
      -1,    -1,    -1,   164,   165,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   183,   184,   185,   186,   187,    -1,   189,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   202,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   237,   238,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,    -1,    -1,
     251,   252,   253,   254,     3,     4,     5,     6,     7,     8,
       9,    -1,    11,    -1,    -1,   266,   267,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    -1,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
      -1,    -1,    -1,    -1,   143,   144,   145,   146,   147,   148,
     149,   150,   151,    -1,   153,   154,   155,   156,   157,   158,
     159,   160,    -1,    -1,    -1,   164,   165,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   183,   184,   185,   186,   187,    -1,
     189,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   202,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   237,   238,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,
      -1,    -1,   251,   252,   253,   254,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,   266,    -1,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   148,
      -1,   150,   151,    -1,   153,   154,   155,   156,   157,   158,
     159,   160,    -1,    -1,    -1,   164,   165,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,
      -1,    -1,   251,   252,   253,   254,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   267,   268,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   148,
      -1,   150,   151,    -1,   153,   154,   155,   156,   157,   158,
     159,   160,    -1,    -1,    -1,   164,   165,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,
      -1,    -1,   251,   252,   253,   254,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   267,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   148,
      -1,   150,   151,    -1,   153,   154,   155,   156,   157,   158,
     159,   160,    -1,    -1,    -1,   164,   165,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   246,    -1,    -1,
      -1,    -1,   251,   252,   253,   254,     3,     4,     5,     6,
       7,     8,     9,    -1,    11,    -1,    -1,   266,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    -1,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,    -1,    -1,    -1,    -1,   143,   144,   145,   146,
     147,   148,   149,   150,   151,    -1,   153,   154,   155,   156,
     157,   158,   159,   160,    -1,    -1,    -1,   164,   165,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   183,   184,   185,   186,
     187,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   202,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     237,   238,     5,     6,     7,     8,     9,    -1,    -1,   246,
      -1,    -1,    -1,    -1,   251,   252,   253,   254,    -1,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   148,    -1,   150,   151,    -1,
     153,   154,   155,   156,   157,   158,   159,   160,    -1,    -1,
      -1,   164,   165,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       5,     6,     7,     8,     9,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    -1,   246,    -1,    -1,   249,    -1,   251,   252,
     253,   254,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   148,    -1,   150,   151,    -1,   153,   154,
     155,   156,   157,   158,   159,   160,    -1,    -1,    -1,   164,
     165,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     5,     6,
       7,     8,     9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      -1,   246,    -1,    -1,   249,    -1,   251,   252,   253,   254,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   148,    -1,   150,   151,    -1,   153,   154,   155,   156,
     157,   158,   159,   160,    -1,    -1,    -1,   164,   165,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     5,     6,     7,     8,
       9,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    -1,   246,
      -1,    -1,    -1,    -1,   251,   252,   253,   254,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   148,
      -1,   150,   151,    -1,   153,   154,   155,   156,   157,   158,
     159,   160,    -1,    -1,    -1,   164,   165,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,
       6,     7,     8,     9,    -1,    11,    -1,   246,    -1,    -1,
      -1,    -1,   251,   252,   253,   254,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    -1,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,    -1,    -1,    -1,    -1,   143,   144,   145,
     146,   147,   148,   149,   150,   151,    -1,    -1,   154,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   183,   184,   185,
     186,   187,    -1,   189,    -1,   191,    -1,    -1,    -1,    -1,
      -1,   197,   198,   199,   200,   201,   202,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,     8,
       9,    -1,    11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   237,   238,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    -1,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
      -1,    -1,    -1,    -1,   143,   144,   145,   146,   147,   148,
     149,   150,   151,    -1,    -1,   154,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   183,   184,   185,   186,   187,    -1,
     189,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   197,   198,
     199,   200,   201,   202,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,     3,     4,     5,     6,     7,     8,     9,    -1,    11,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   237,   238,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    -1,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,    -1,    -1,    -1,
      -1,   143,   144,   145,   146,   147,   148,   149,   150,   151,
      -1,    -1,   154,     3,     4,    -1,    -1,    -1,    -1,    -1,
      -1,    11,    -1,     3,     4,    -1,    -1,    -1,    -1,    -1,
      -1,    11,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,   183,   184,   185,   186,   187,    -1,    37,    38,    39,
      -1,    41,    42,    43,    44,    45,    46,    37,    38,    39,
     202,    41,    42,    43,    44,    45,    46,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     3,     4,    -1,    -1,    -1,    -1,    -1,
      -1,    11,    -1,    -1,    -1,   237,   238,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    37,    38,    39,
      -1,    41,    42,    43,    44,    45,    46,   264,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   143,   144,   145,   146,   147,    -1,   149,
      -1,   298,   299,   143,   144,   145,   146,   147,    -1,   149,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   319,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   183,   184,   185,   186,   187,    -1,    -1,
      -1,    -1,    -1,   183,   184,   185,   186,   187,    -1,    -1,
      -1,    -1,   202,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   202,   143,   144,   145,   146,   147,    -1,   149,
      -1,    -1,   369,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   380,    -1,    -1,    -1,   237,   238,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   237,   238,    -1,
      -1,    -1,    -1,   183,   184,   185,   186,   187,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   202,    -1,    -1,    -1,   423,   424,   425,   426,
     427,   428,   429,   430,   431,   432,   433,   434,   435,   436,
     437,   438,   439,   440,   441,     5,     6,     7,     8,     9,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   237,   238,    -1,
      -1,    -1,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   148,    -1,
     150,   151,    -1,    -1,   154
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint16 yystos[] =
{
       0,   190,   270,   272,   158,     0,   274,   153,   154,   155,
     194,   275,   191,   271,   276,   194,   275,     3,     4,     5,
       6,     7,     8,     9,    11,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     154,   183,   184,   185,   186,   187,   189,   197,   198,   199,
     200,   201,   202,   237,   238,   273,   277,   307,   308,   309,
     310,   311,   317,   318,   319,   320,   324,   326,   327,   328,
     329,   330,   332,   333,   334,   335,   336,   366,   367,   368,
     369,   370,   371,   376,   377,   378,   379,   380,   193,   267,
     275,    11,    38,    39,    41,   153,   155,   183,   184,   278,
     320,   327,   330,   278,   327,   335,   194,   194,   194,   194,
     194,   246,   246,   276,   366,   266,   267,   349,   247,   250,
       4,    38,    39,    40,   184,   313,   314,   315,   330,   335,
     250,   266,   153,   155,   275,   278,    11,    38,    39,    41,
     327,   368,   376,   377,   378,   379,   327,   327,   332,   327,
     327,   320,   327,   368,   248,   331,   327,   155,   155,   275,
     319,   337,   338,   267,   332,   149,   211,   244,   275,   321,
     322,   323,   275,   325,    10,    12,    13,    15,    16,    17,
      18,    19,   152,   156,   157,   158,   159,   160,   164,   165,
     246,   251,   252,   253,   254,   266,   267,   268,   278,   279,
     280,   282,   283,   284,   285,   286,   287,   288,   289,   290,
     291,   292,   293,   294,   295,   296,   297,   298,   299,   300,
     301,   302,   303,   305,   307,   308,   332,   343,   344,   345,
     346,   350,   351,   352,   355,   361,   365,   313,   314,   314,
     312,   316,   332,   314,   314,   314,   275,   265,   331,   246,
     266,   266,   266,   266,   249,   289,   302,   306,   332,   248,
     267,   267,   194,   275,   339,   340,   268,   338,   337,   266,
     265,   247,   250,   247,   250,   266,   266,   344,   246,   246,
     266,   266,   305,   246,   246,   289,   289,   305,   268,   347,
     164,   165,   203,   248,   247,   247,   250,   151,   303,   246,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     265,   304,   289,   255,   256,   257,   251,   252,   162,   163,
     166,   167,   258,   259,   168,   169,   260,   261,   262,   170,
     172,   171,   263,   250,   266,   268,   344,   275,   265,   331,
     267,   303,   341,   265,   249,   249,   306,   319,   374,   375,
     374,   331,   250,   266,   268,   306,   322,   275,   152,   343,
     351,   362,   305,   266,   305,   305,   319,   354,   247,   350,
     161,   281,   305,   303,   303,   289,   289,   289,   291,   291,
     292,   292,   293,   293,   293,   293,   294,   294,   295,   296,
     297,   298,   299,   300,   305,   303,   331,   341,   265,   341,
     342,   341,   249,   339,   268,   374,   268,   340,   246,   354,
     363,   364,   247,   247,   275,   247,   268,   249,   264,   341,
     250,   268,   266,   155,   372,   155,   373,   305,   266,   247,
     344,   353,   267,   356,   265,   345,   348,   349,   303,   268,
     341,   331,   266,   331,   266,   247,   305,   348,    14,    20,
      21,   268,   357,   358,   359,   360,   341,   266,   344,   305,
     264,   344,   357,   344,   268,   359,   264
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint16 yyr1[] =
{
       0,   269,   271,   270,   272,   272,   272,   273,   273,   273,
     273,   273,   274,   274,   275,   275,   275,   276,   277,   277,
     277,   278,   278,   279,   279,   279,   279,   279,   279,   279,
     280,   280,   280,   280,   280,   280,   281,   282,   283,   284,
     284,   285,   285,   286,   286,   287,   288,   288,   289,   289,
     289,   289,   290,   290,   290,   290,   291,   291,   291,   291,
     292,   292,   292,   293,   293,   293,   294,   294,   294,   294,
     294,   295,   295,   295,   296,   296,   297,   297,   298,   298,
     299,   299,   300,   300,   301,   301,   302,   302,   303,   303,
     304,   304,   304,   304,   304,   304,   304,   304,   304,   304,
     304,   305,   305,   306,   307,   307,   307,   307,   308,   309,
     309,   310,   310,   311,   312,   312,   313,   313,   314,   314,
     314,   314,   314,   314,   315,   315,   315,   316,   317,   317,
     317,   317,   317,   318,   318,   318,   318,   318,   318,   318,
     319,   319,   320,   321,   321,   322,   322,   322,   323,   323,
     323,   324,   324,   325,   325,   326,   326,   326,   327,   327,
     327,   327,   327,   327,   327,   327,   327,   327,   327,   327,
     327,   327,   327,   327,   327,   327,   328,   328,   328,   329,
     329,   329,   329,   329,   329,   329,   329,   330,   330,   330,
     330,   330,   331,   331,   331,   331,   332,   332,   333,   333,
     333,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   334,   334,   334,   334,   334,
     334,   334,   334,   334,   334,   335,   335,   335,   336,   336,
     337,   337,   338,   339,   339,   340,   340,   341,   341,   341,
     342,   342,   343,   344,   344,   345,   345,   345,   345,   345,
     345,   346,   347,   346,   348,   348,   349,   349,   350,   350,
     351,   351,   352,   353,   353,   354,   354,   355,   356,   356,
     357,   357,   358,   358,   359,   359,   360,   360,   361,   361,
     361,   362,   362,   363,   363,   364,   364,   365,   365,   365,
     365,   365,   366,   366,   366,   366,   367,   368,   368,   368,
     369,   369,   370,   370,   370,   371,   372,   372,   372,   373,
     373,   373,   374,   374,   375,   376,   376,   377,   377,   378,
     378,   379,   379,   380,   380,   380,   380
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     0,     4,     0,     3,     4,     2,     2,     2,
       2,     2,     0,     2,     1,     1,     1,     5,     1,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     4,     1,     3,     2,     2,     1,     1,     1,     2,
       2,     2,     1,     2,     3,     2,     1,     1,     1,     2,
       2,     2,     1,     1,     1,     1,     1,     3,     3,     3,
       1,     3,     3,     1,     3,     3,     1,     3,     3,     3,
       3,     1,     3,     3,     1,     3,     1,     3,     1,     3,
       1,     3,     1,     3,     1,     3,     1,     5,     1,     3,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     1,     2,     2,     4,     1,     2,     1,
       1,     2,     3,     3,     2,     3,     2,     2,     0,     2,
       2,     2,     2,     2,     1,     1,     1,     1,     1,     3,
       4,     6,     5,     1,     2,     3,     5,     4,     2,     2,
       1,     2,     4,     1,     3,     1,     3,     1,     1,     1,
       1,     1,     4,     1,     3,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     3,     3,     4,     1,     2,     1,     1,
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
       1,     1,     1,     1,     1,     1,     1,     1,     5,     4,
       1,     2,     3,     1,     3,     1,     2,     1,     3,     4,
       1,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     2,     0,     4,     1,     1,     2,     3,     1,     2,
       1,     2,     5,     3,     1,     1,     4,     5,     2,     3,
       3,     2,     1,     2,     2,     2,     1,     2,     5,     7,
       6,     1,     1,     1,     0,     2,     3,     2,     2,     2,
       3,     2,     1,     1,     1,     1,     2,     1,     2,     2,
       7,     7,     1,     1,     1,     1,     0,     1,     2,     0,
       1,     2,     1,     2,     3,     2,     3,     2,     3,     2,
       3,     2,     3,     1,     1,     1,     1
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
#line 87 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1429  */
{
   yylloc.first_line = 1;
   yylloc.first_column = 1;
   yylloc.last_line = 1;
   yylloc.last_column = 1;
   yylloc.source = 0;
}

#line 3105 "glsl/glsl_parser.cpp" /* yacc.c:1429  */
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
#line 312 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      _mesa_glsl_initialize_types(state);
   }
#line 3296 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 3:
#line 316 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 3318 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 5:
#line 338 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->process_version_directive(&(yylsp[-1]), (yyvsp[-1].n), NULL);
      if (state->error) {
         YYERROR;
      }
   }
#line 3329 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 6:
#line 345 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->process_version_directive(&(yylsp[-2]), (yyvsp[-2].n), (yyvsp[-1].identifier));
      if (state->error) {
         YYERROR;
      }
   }
#line 3340 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 11:
#line 359 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 3366 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 17:
#line 395 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!_mesa_glsl_process_extension((yyvsp[-3].identifier), & (yylsp[-3]), (yyvsp[-1].identifier), & (yylsp[-1]), state)) {
         YYERROR;
      }
   }
#line 3376 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 18:
#line 404 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* FINISHME: The NULL test is required because pragmas are set to
       * FINISHME: NULL. (See production rule for external_declaration.)
       */
      if ((yyvsp[0].node) != NULL)
         state->translation_unit.push_tail(& (yyvsp[0].node)->link);
   }
#line 3388 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 19:
#line 412 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      /* FINISHME: The NULL test is required because pragmas are set to
       * FINISHME: NULL. (See production rule for external_declaration.)
       */
      if ((yyvsp[0].node) != NULL)
         state->translation_unit.push_tail(& (yyvsp[0].node)->link);
   }
#line 3400 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 20:
#line 419 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!state->allow_extension_directive_midshader) {
         _mesa_glsl_error(& (yylsp[0]), state,
                          "#extension directive is not allowed "
                          "in the middle of a shader");
         YYERROR;
      }
   }
#line 3413 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 23:
#line 436 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_identifier, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.identifier = (yyvsp[0].identifier);
   }
#line 3424 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 24:
#line 443 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_int_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.int_constant = (yyvsp[0].n);
   }
#line 3435 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 25:
#line 450 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_uint_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.uint_constant = (yyvsp[0].n);
   }
#line 3446 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 26:
#line 457 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_float_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.float_constant = (yyvsp[0].real);
   }
#line 3457 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 27:
#line 464 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_double_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.double_constant = (yyvsp[0].dreal);
   }
#line 3468 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 28:
#line 471 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_bool_constant, NULL, NULL, NULL);
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->primary_expression.bool_constant = (yyvsp[0].n);
   }
#line 3479 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 29:
#line 478 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
   }
#line 3487 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 31:
#line 486 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_array_index, (yyvsp[-3].expression), (yyvsp[-1].expression), NULL);
      (yyval.expression)->set_location_range((yylsp[-3]), (yylsp[0]));
   }
#line 3497 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 32:
#line 492 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[0].expression);
   }
#line 3505 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 33:
#line 496 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_field_selection, (yyvsp[-2].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.expression)->primary_expression.identifier = (yyvsp[0].identifier);
   }
#line 3516 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 34:
#line 503 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_post_inc, (yyvsp[-1].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 3526 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 35:
#line 509 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_post_dec, (yyvsp[-1].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 3536 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 43:
#line 540 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
      (yyval.expression)->set_location((yylsp[-1]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 3546 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 44:
#line 546 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-2].expression);
      (yyval.expression)->set_location((yylsp[-2]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 3556 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 46:
#line 562 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_function_expression((yyvsp[0].type_specifier));
      (yyval.expression)->set_location((yylsp[0]));
      }
#line 3566 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 47:
#line 568 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_function_expression((yyvsp[0].expression));
      (yyval.expression)->set_location((yylsp[0]));
      }
#line 3576 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 49:
#line 583 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_pre_inc, (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location((yylsp[-1]));
   }
#line 3586 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 50:
#line 589 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_pre_dec, (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location((yylsp[-1]));
   }
#line 3596 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 51:
#line 595 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression((yyvsp[-1].n), (yyvsp[0].expression), NULL, NULL);
      (yyval.expression)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 3606 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 52:
#line 604 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_plus; }
#line 3612 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 53:
#line 605 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_neg; }
#line 3618 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 54:
#line 606 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_logic_not; }
#line 3624 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 55:
#line 607 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_bit_not; }
#line 3630 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 57:
#line 613 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_mul, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3640 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 58:
#line 619 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_div, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3650 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 59:
#line 625 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_mod, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3660 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 61:
#line 635 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_add, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3670 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 62:
#line 641 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_sub, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3680 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 64:
#line 651 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_lshift, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3690 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 65:
#line 657 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_rshift, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3700 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 67:
#line 667 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_less, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3710 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 68:
#line 673 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_greater, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3720 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 69:
#line 679 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_lequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3730 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 70:
#line 685 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_gequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3740 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 72:
#line 695 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_equal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3750 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 73:
#line 701 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_nequal, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3760 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 75:
#line 711 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_and, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3770 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 77:
#line 721 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_xor, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3780 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 79:
#line 731 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_bit_or, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3790 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 81:
#line 741 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_and, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3800 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 83:
#line 751 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_xor, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3810 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 85:
#line 761 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression_bin(ast_logic_or, (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3820 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 87:
#line 771 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression(ast_conditional, (yyvsp[-4].expression), (yyvsp[-2].expression), (yyvsp[0].expression));
      (yyval.expression)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 3830 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 89:
#line 781 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_expression((yyvsp[-1].n), (yyvsp[-2].expression), (yyvsp[0].expression), NULL);
      (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 3840 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 90:
#line 789 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_assign; }
#line 3846 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 91:
#line 790 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_mul_assign; }
#line 3852 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 92:
#line 791 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_div_assign; }
#line 3858 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 93:
#line 792 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_mod_assign; }
#line 3864 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 94:
#line 793 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_add_assign; }
#line 3870 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 95:
#line 794 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_sub_assign; }
#line 3876 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 96:
#line 795 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_ls_assign; }
#line 3882 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 97:
#line 796 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_rs_assign; }
#line 3888 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 98:
#line 797 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_and_assign; }
#line 3894 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 99:
#line 798 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_xor_assign; }
#line 3900 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 100:
#line 799 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.n) = ast_or_assign; }
#line 3906 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 101:
#line 804 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[0].expression);
   }
#line 3914 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 102:
#line 808 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      if ((yyvsp[-2].expression)->oper != ast_sequence) {
         (yyval.expression) = new(ctx) ast_expression(ast_sequence, NULL, NULL, NULL);
         (yyval.expression)->set_location_range((yylsp[-2]), (yylsp[0]));
         (yyval.expression)->expressions.push_tail(& (yyvsp[-2].expression)->link);
      } else {
         (yyval.expression) = (yyvsp[-2].expression);
      }

      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 3931 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 104:
#line 828 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->symbols->pop_scope();
      (yyval.node) = (yyvsp[-1].function);
   }
#line 3940 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 105:
#line 833 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[-1].declarator_list);
   }
#line 3948 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 106:
#line 837 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyvsp[-1].type_specifier)->default_precision = (yyvsp[-2].n);
      (yyval.node) = (yyvsp[-1].type_specifier);
   }
#line 3957 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 107:
#line 842 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[0].node);
   }
#line 3965 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 111:
#line 858 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.function) = (yyvsp[-1].function);
      (yyval.function)->parameters.push_tail(& (yyvsp[0].parameter_declarator)->link);
   }
#line 3974 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 112:
#line 863 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.function) = (yyvsp[-2].function);
      (yyval.function)->parameters.push_tail(& (yyvsp[0].parameter_declarator)->link);
   }
#line 3983 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 113:
#line 871 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.function) = new(ctx) ast_function();
      (yyval.function)->set_location((yylsp[-1]));
      (yyval.function)->return_type = (yyvsp[-2].fully_specified_type);
      (yyval.function)->identifier = (yyvsp[-1].identifier);

      if ((yyvsp[-2].fully_specified_type)->qualifier.flags.q.subroutine) {
         /* add type for IDENTIFIER search */
         state->symbols->add_type((yyvsp[-1].identifier), glsl_type::get_subroutine_instance((yyvsp[-1].identifier)));
      } else
         state->symbols->add_function(new(state) ir_function((yyvsp[-1].identifier)));
      state->symbols->push_scope();
   }
#line 4002 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 114:
#line 889 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.parameter_declarator) = new(ctx) ast_parameter_declarator();
      (yyval.parameter_declarator)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.parameter_declarator)->type = new(ctx) ast_fully_specified_type();
      (yyval.parameter_declarator)->type->set_location((yylsp[-1]));
      (yyval.parameter_declarator)->type->specifier = (yyvsp[-1].type_specifier);
      (yyval.parameter_declarator)->identifier = (yyvsp[0].identifier);
   }
#line 4016 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 115:
#line 899 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.parameter_declarator) = new(ctx) ast_parameter_declarator();
      (yyval.parameter_declarator)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.parameter_declarator)->type = new(ctx) ast_fully_specified_type();
      (yyval.parameter_declarator)->type->set_location((yylsp[-2]));
      (yyval.parameter_declarator)->type->specifier = (yyvsp[-2].type_specifier);
      (yyval.parameter_declarator)->identifier = (yyvsp[-1].identifier);
      (yyval.parameter_declarator)->array_specifier = (yyvsp[0].array_specifier);
   }
#line 4031 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 116:
#line 913 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.parameter_declarator) = (yyvsp[0].parameter_declarator);
      (yyval.parameter_declarator)->type->qualifier = (yyvsp[-1].type_qualifier);
   }
#line 4040 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 117:
#line 918 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.parameter_declarator) = new(ctx) ast_parameter_declarator();
      (yyval.parameter_declarator)->set_location((yylsp[0]));
      (yyval.parameter_declarator)->type = new(ctx) ast_fully_specified_type();
      (yyval.parameter_declarator)->type->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.parameter_declarator)->type->qualifier = (yyvsp[-1].type_qualifier);
      (yyval.parameter_declarator)->type->specifier = (yyvsp[0].type_specifier);
   }
#line 4054 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 118:
#line 931 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
   }
#line 4062 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 119:
#line 935 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.constant)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate const qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.constant = 1;
   }
#line 4074 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 120:
#line 943 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precise qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 4086 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 121:
#line 951 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (((yyvsp[-1].type_qualifier).flags.q.in || (yyvsp[-1].type_qualifier).flags.q.out) && ((yyvsp[0].type_qualifier).flags.q.in || (yyvsp[0].type_qualifier).flags.q.out))
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate in/out/inout qualifier");

      if (!state->has_420pack_or_es31() && (yyvsp[0].type_qualifier).flags.q.constant)
         _mesa_glsl_error(&(yylsp[-1]), state, "in/out/inout must come after const "
                                      "or precise");

      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4102 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 122:
#line 963 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).precision != ast_precision_none)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precision qualifier");

      if (!state->has_420pack_or_es31() &&
          (yyvsp[0].type_qualifier).flags.i != 0)
         _mesa_glsl_error(&(yylsp[-1]), state, "precision qualifiers must come last");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).precision = (yyvsp[-1].n);
   }
#line 4118 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 123:
#line 975 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 4127 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 124:
#line 982 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 4136 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 125:
#line 987 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 4145 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 126:
#line 992 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 4155 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 129:
#line 1006 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = (yyvsp[-2].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[0].identifier), ir_var_auto));
   }
#line 4169 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 130:
#line 1016 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      decl->set_location_range((yylsp[-1]), (yylsp[0]));

      (yyval.declarator_list) = (yyvsp[-3].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-1].identifier), ir_var_auto));
   }
#line 4183 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 131:
#line 1026 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-3].identifier), (yyvsp[-2].array_specifier), (yyvsp[0].expression));
      decl->set_location_range((yylsp[-3]), (yylsp[-2]));

      (yyval.declarator_list) = (yyvsp[-5].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-3].identifier), ir_var_auto));
   }
#line 4197 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 132:
#line 1036 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      decl->set_location((yylsp[-2]));

      (yyval.declarator_list) = (yyvsp[-4].declarator_list);
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
      state->symbols->add_variable(new(state) ir_variable(NULL, (yyvsp[-2].identifier), ir_var_auto));
   }
#line 4211 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 133:
#line 1050 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      /* Empty declaration list is valid. */
      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[0].fully_specified_type));
      (yyval.declarator_list)->set_location((yylsp[0]));
   }
#line 4222 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 134:
#line 1057 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-1].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4236 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 135:
#line 1067 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      decl->set_location_range((yylsp[-1]), (yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-2].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-2]), (yylsp[0]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4250 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 136:
#line 1077 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-3].identifier), (yyvsp[-2].array_specifier), (yyvsp[0].expression));
      decl->set_location_range((yylsp[-3]), (yylsp[-2]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-4].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-4]), (yylsp[-2]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4264 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 137:
#line 1087 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      decl->set_location((yylsp[-2]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list((yyvsp[-3].fully_specified_type));
      (yyval.declarator_list)->set_location_range((yylsp[-3]), (yylsp[-2]));
      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4278 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 138:
#line 1097 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list(NULL);
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->invariant = true;

      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4294 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 139:
#line 1109 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      decl->set_location((yylsp[0]));

      (yyval.declarator_list) = new(ctx) ast_declarator_list(NULL);
      (yyval.declarator_list)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.declarator_list)->precise = true;

      (yyval.declarator_list)->declarations.push_tail(&decl->link);
   }
#line 4310 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 140:
#line 1124 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.fully_specified_type) = new(ctx) ast_fully_specified_type();
      (yyval.fully_specified_type)->set_location((yylsp[0]));
      (yyval.fully_specified_type)->specifier = (yyvsp[0].type_specifier);
   }
#line 4321 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 141:
#line 1131 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.fully_specified_type) = new(ctx) ast_fully_specified_type();
      (yyval.fully_specified_type)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.fully_specified_type)->qualifier = (yyvsp[-1].type_qualifier);
      (yyval.fully_specified_type)->specifier = (yyvsp[0].type_specifier);
      if ((yyval.fully_specified_type)->specifier->structure != NULL &&
          (yyval.fully_specified_type)->specifier->structure->is_declaration) {
            (yyval.fully_specified_type)->specifier->structure->layout = &(yyval.fully_specified_type)->qualifier;
      }
   }
#line 4337 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 142:
#line 1146 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
   }
#line 4345 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 144:
#line 1154 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-2].type_qualifier);
      if (!(yyval.type_qualifier).merge_qualifier(& (yylsp[0]), state, (yyvsp[0].type_qualifier), true)) {
         YYERROR;
      }
   }
#line 4356 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 145:
#line 1164 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
            (yyval.type_qualifier).flags.q.depth_any = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "depth_greater", state) == 0) {
            (yyval.type_qualifier).flags.q.depth_greater = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "depth_less", state) == 0) {
            (yyval.type_qualifier).flags.q.depth_less = 1;
         } else if (match_layout_qualifier((yyvsp[0].identifier), "depth_unchanged",
                                           state) == 0) {
            (yyval.type_qualifier).flags.q.depth_unchanged = 1;
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

         if ((yyval.type_qualifier).flags.i && !state->has_geometry_shader()) {
            _mesa_glsl_error(& (yylsp[0]), state, "#version 150 layout "
                             "qualifier `%s' used", (yyvsp[0].identifier));
         }
      }

      /* Layout qualifiers for ARB_shader_image_load_store. */
      if (state->ARB_shader_image_load_store_enable ||
          state->is_version(420, 310)) {
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
            } map[] = {
               { "rgba32f", GL_RGBA32F, GLSL_TYPE_FLOAT, 130, 310 },
               { "rgba16f", GL_RGBA16F, GLSL_TYPE_FLOAT, 130, 310 },
               { "rg32f", GL_RG32F, GLSL_TYPE_FLOAT, 130, 0 },
               { "rg16f", GL_RG16F, GLSL_TYPE_FLOAT, 130, 0 },
               { "r11f_g11f_b10f", GL_R11F_G11F_B10F, GLSL_TYPE_FLOAT, 130, 0 },
               { "r32f", GL_R32F, GLSL_TYPE_FLOAT, 130, 310 },
               { "r16f", GL_R16F, GLSL_TYPE_FLOAT, 130, 0 },
               { "rgba32ui", GL_RGBA32UI, GLSL_TYPE_UINT, 130, 310 },
               { "rgba16ui", GL_RGBA16UI, GLSL_TYPE_UINT, 130, 310 },
               { "rgb10_a2ui", GL_RGB10_A2UI, GLSL_TYPE_UINT, 130, 0 },
               { "rgba8ui", GL_RGBA8UI, GLSL_TYPE_UINT, 130, 310 },
               { "rg32ui", GL_RG32UI, GLSL_TYPE_UINT, 130, 0 },
               { "rg16ui", GL_RG16UI, GLSL_TYPE_UINT, 130, 0 },
               { "rg8ui", GL_RG8UI, GLSL_TYPE_UINT, 130, 0 },
               { "r32ui", GL_R32UI, GLSL_TYPE_UINT, 130, 310 },
               { "r16ui", GL_R16UI, GLSL_TYPE_UINT, 130, 0 },
               { "r8ui", GL_R8UI, GLSL_TYPE_UINT, 130, 0 },
               { "rgba32i", GL_RGBA32I, GLSL_TYPE_INT, 130, 310 },
               { "rgba16i", GL_RGBA16I, GLSL_TYPE_INT, 130, 310 },
               { "rgba8i", GL_RGBA8I, GLSL_TYPE_INT, 130, 310 },
               { "rg32i", GL_RG32I, GLSL_TYPE_INT, 130, 0 },
               { "rg16i", GL_RG16I, GLSL_TYPE_INT, 130, 0 },
               { "rg8i", GL_RG8I, GLSL_TYPE_INT, 130, 0 },
               { "r32i", GL_R32I, GLSL_TYPE_INT, 130, 310 },
               { "r16i", GL_R16I, GLSL_TYPE_INT, 130, 0 },
               { "r8i", GL_R8I, GLSL_TYPE_INT, 130, 0 },
               { "rgba16", GL_RGBA16, GLSL_TYPE_FLOAT, 130, 0 },
               { "rgb10_a2", GL_RGB10_A2, GLSL_TYPE_FLOAT, 130, 0 },
               { "rgba8", GL_RGBA8, GLSL_TYPE_FLOAT, 130, 310 },
               { "rg16", GL_RG16, GLSL_TYPE_FLOAT, 130, 0 },
               { "rg8", GL_RG8, GLSL_TYPE_FLOAT, 130, 0 },
               { "r16", GL_R16, GLSL_TYPE_FLOAT, 130, 0 },
               { "r8", GL_R8, GLSL_TYPE_FLOAT, 130, 0 },
               { "rgba16_snorm", GL_RGBA16_SNORM, GLSL_TYPE_FLOAT, 130, 0 },
               { "rgba8_snorm", GL_RGBA8_SNORM, GLSL_TYPE_FLOAT, 130, 310 },
               { "rg16_snorm", GL_RG16_SNORM, GLSL_TYPE_FLOAT, 130, 0 },
               { "rg8_snorm", GL_RG8_SNORM, GLSL_TYPE_FLOAT, 130, 0 },
               { "r16_snorm", GL_R16_SNORM, GLSL_TYPE_FLOAT, 130, 0 },
               { "r8_snorm", GL_R8_SNORM, GLSL_TYPE_FLOAT, 130, 0 }
            };

            for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
               if (state->is_version(map[i].required_glsl,
                                     map[i].required_essl) &&
                   match_layout_qualifier((yyvsp[0].identifier), map[i].name, state) == 0) {
                  (yyval.type_qualifier).flags.q.explicit_image_format = 1;
                  (yyval.type_qualifier).image_format = map[i].format;
                  (yyval.type_qualifier).image_base_type = map[i].base_type;
                  break;
               }
            }
         }

         if (!(yyval.type_qualifier).flags.i &&
             match_layout_qualifier((yyvsp[0].identifier), "early_fragment_tests", state) == 0) {
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
      }

      /* Layout qualifiers for tessellation evaluation shaders. */
      if (!(yyval.type_qualifier).flags.i) {
         struct {
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

         if ((yyval.type_qualifier).flags.i &&
             !state->ARB_tessellation_shader_enable &&
             !state->is_version(400, 0)) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "primitive mode qualifier `%s' requires "
                             "GLSL 4.00 or ARB_tessellation_shader", (yyvsp[0].identifier));
         }
      }
      if (!(yyval.type_qualifier).flags.i) {
         struct {
            const char *s;
            GLenum e;
         } map[] = {
                 { "equal_spacing", GL_EQUAL },
                 { "fractional_odd_spacing", GL_FRACTIONAL_ODD },
                 { "fractional_even_spacing", GL_FRACTIONAL_EVEN },
         };
         for (unsigned i = 0; i < ARRAY_SIZE(map); i++) {
            if (match_layout_qualifier((yyvsp[0].identifier), map[i].s, state) == 0) {
               (yyval.type_qualifier).flags.q.vertex_spacing = 1;
               (yyval.type_qualifier).vertex_spacing = map[i].e;
               break;
            }
         }

         if ((yyval.type_qualifier).flags.i &&
             !state->ARB_tessellation_shader_enable &&
             !state->is_version(400, 0)) {
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

         if ((yyval.type_qualifier).flags.i &&
             !state->ARB_tessellation_shader_enable &&
             !state->is_version(400, 0)) {
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

         if ((yyval.type_qualifier).flags.i &&
             !state->ARB_tessellation_shader_enable &&
             !state->is_version(400, 0)) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "qualifier `point_mode' requires "
                             "GLSL 4.00 or ARB_tessellation_shader");
         }
      }

      if (!(yyval.type_qualifier).flags.i) {
         _mesa_glsl_error(& (yylsp[0]), state, "unrecognized layout identifier "
                          "`%s'", (yyvsp[0].identifier));
         YYERROR;
      }
   }
#line 4655 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 146:
#line 1459 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      void *ctx = state;

      if ((yyvsp[0].expression)->oper != ast_int_constant &&
          (yyvsp[0].expression)->oper != ast_uint_constant &&
          !state->has_enhanced_layouts()) {
         _mesa_glsl_error(& (yylsp[-2]), state,
                          "compile-time constant expressions require "
                          "GLSL 4.40 or ARB_enhanced_layouts");
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

      if (state->has_atomic_counters() &&
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
         if (!state->is_version(400, 0) &&
             !state->ARB_gpu_shader5_enable) {
            _mesa_glsl_error(& (yylsp[0]), state,
                             "GL_ARB_gpu_shader5 invocations "
                             "qualifier specified", (yyvsp[0].expression));
         }
      }

      /* Layout qualifiers for tessellation control shaders. */
      if (match_layout_qualifier("vertices", (yyvsp[-2].identifier), state) == 0) {
         (yyval.type_qualifier).flags.q.vertices = 1;
         (yyval.type_qualifier).vertices = new(ctx) ast_layout_expression((yylsp[-2]), (yyvsp[0].expression));
         if (!state->ARB_tessellation_shader_enable &&
             !state->is_version(400, 0)) {
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
#line 4781 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 147:
#line 1581 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4799 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 148:
#line 1607 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.row_major = 1;
   }
#line 4808 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 149:
#line 1612 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.packed = 1;
   }
#line 4817 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 150:
#line 1617 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.shared = 1;
   }
#line 4826 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 151:
#line 1625 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.subroutine = 1;
   }
#line 4835 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 152:
#line 1630 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.subroutine_def = 1;
      (yyval.type_qualifier).subroutine_list = (yyvsp[-1].subroutine_list);
   }
#line 4845 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 153:
#line 1639 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
        void *ctx = state;
        ast_declaration *decl = new(ctx)  ast_declaration((yyvsp[0].identifier), NULL, NULL);
        decl->set_location((yylsp[0]));

        (yyval.subroutine_list) = new(ctx) ast_subroutine_list();
        (yyval.subroutine_list)->declarations.push_tail(&decl->link);
   }
#line 4858 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 154:
#line 1648 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
        void *ctx = state;
        ast_declaration *decl = new(ctx)  ast_declaration((yyvsp[0].identifier), NULL, NULL);
        decl->set_location((yylsp[0]));

        (yyval.subroutine_list) = (yyvsp[-2].subroutine_list);
        (yyval.subroutine_list)->declarations.push_tail(&decl->link);
   }
#line 4871 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 155:
#line 1660 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.smooth = 1;
   }
#line 4880 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 156:
#line 1665 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.flat = 1;
   }
#line 4889 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 157:
#line 1670 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.noperspective = 1;
   }
#line 4898 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 158:
#line 1679 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.invariant = 1;
   }
#line 4907 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 159:
#line 1684 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 4916 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 166:
#line 1695 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(&(yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).precision = (yyvsp[0].n);
   }
#line 4925 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 167:
#line 1713 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).flags.q.precise)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate \"precise\" qualifier");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).flags.q.precise = 1;
   }
#line 4937 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 168:
#line 1721 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
       */
      if (state->es_shader && state->language_version >= 300 && (yyval.type_qualifier).flags.q.in)
         _mesa_glsl_error(&(yylsp[-1]), state, "invariant qualifiers cannot be used with shader inputs");
   }
#line 4964 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 169:
#line 1744 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 4992 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 170:
#line 1768 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
      if (!state->has_420pack_or_es31() && (yyvsp[0].type_qualifier).has_layout())
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate layout(...) qualifiers");

      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 5013 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 171:
#line 1785 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 5022 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 172:
#line 1790 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5042 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 173:
#line 1806 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
#line 5066 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 174:
#line 1826 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].type_qualifier).precision != ast_precision_none)
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate precision qualifier");

      if (!(state->has_420pack_or_es31()) &&
          (yyvsp[0].type_qualifier).flags.i != 0)
         _mesa_glsl_error(&(yylsp[-1]), state, "precision qualifiers must come last");

      (yyval.type_qualifier) = (yyvsp[0].type_qualifier);
      (yyval.type_qualifier).precision = (yyvsp[-1].n);
   }
#line 5082 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 175:
#line 1838 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_qualifier) = (yyvsp[-1].type_qualifier);
      (yyval.type_qualifier).merge_qualifier(&(yylsp[-1]), state, (yyvsp[0].type_qualifier), false);
   }
#line 5091 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 176:
#line 1846 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.centroid = 1;
   }
#line 5100 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 177:
#line 1851 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.sample = 1;
   }
#line 5109 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 178:
#line 1856 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.patch = 1;
   }
#line 5118 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 179:
#line 1863 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.constant = 1;
   }
#line 5127 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 180:
#line 1868 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.attribute = 1;
   }
#line 5136 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 181:
#line 1873 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.varying = 1;
   }
#line 5145 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 182:
#line 1878 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 5154 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 183:
#line 1883 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
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
   }
#line 5177 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 184:
#line 1902 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.uniform = 1;
   }
#line 5186 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 185:
#line 1907 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.buffer = 1;
   }
#line 5195 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 186:
#line 1912 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.shared_storage = 1;
   }
#line 5204 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 187:
#line 1920 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.coherent = 1;
   }
#line 5213 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 188:
#line 1925 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q._volatile = 1;
   }
#line 5222 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 189:
#line 1930 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      STATIC_ASSERT(sizeof((yyval.type_qualifier).flags.q) <= sizeof((yyval.type_qualifier).flags.i));
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.restrict_flag = 1;
   }
#line 5232 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 190:
#line 1936 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.read_only = 1;
   }
#line 5241 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 191:
#line 1941 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.write_only = 1;
   }
#line 5250 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 192:
#line 1949 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.array_specifier) = new(ctx) ast_array_specifier((yylsp[-1]), new(ctx) ast_expression(
                                                  ast_unsized_array_dim, NULL,
                                                  NULL, NULL));
      (yyval.array_specifier)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 5262 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 193:
#line 1957 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.array_specifier) = new(ctx) ast_array_specifier((yylsp[-2]), (yyvsp[-1].expression));
      (yyval.array_specifier)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 5272 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 194:
#line 1963 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.array_specifier) = (yyvsp[-2].array_specifier);

      if (state->check_arrays_of_arrays_allowed(& (yylsp[-2]))) {
         (yyval.array_specifier)->add_dimension(new(ctx) ast_expression(ast_unsized_array_dim, NULL,
                                                   NULL, NULL));
      }
   }
#line 5286 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 195:
#line 1973 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.array_specifier) = (yyvsp[-3].array_specifier);

      if (state->check_arrays_of_arrays_allowed(& (yylsp[-3]))) {
         (yyval.array_specifier)->add_dimension((yyvsp[-1].expression));
      }
   }
#line 5298 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 197:
#line 1985 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.type_specifier) = (yyvsp[-1].type_specifier);
      (yyval.type_specifier)->array_specifier = (yyvsp[0].array_specifier);
   }
#line 5307 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 198:
#line 1993 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].identifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 5317 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 199:
#line 1999 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].struct_specifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 5327 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 200:
#line 2005 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.type_specifier) = new(ctx) ast_type_specifier((yyvsp[0].identifier));
      (yyval.type_specifier)->set_location((yylsp[0]));
   }
#line 5337 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 201:
#line 2013 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "void"; }
#line 5343 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 202:
#line 2014 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "float"; }
#line 5349 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 203:
#line 2015 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "double"; }
#line 5355 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 204:
#line 2016 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "int"; }
#line 5361 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 205:
#line 2017 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uint"; }
#line 5367 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 206:
#line 2018 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bool"; }
#line 5373 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 207:
#line 2019 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "vec2"; }
#line 5379 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 208:
#line 2020 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "vec3"; }
#line 5385 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 209:
#line 2021 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "vec4"; }
#line 5391 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 210:
#line 2022 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bvec2"; }
#line 5397 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 211:
#line 2023 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bvec3"; }
#line 5403 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 212:
#line 2024 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "bvec4"; }
#line 5409 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 213:
#line 2025 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "ivec2"; }
#line 5415 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 214:
#line 2026 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "ivec3"; }
#line 5421 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 215:
#line 2027 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "ivec4"; }
#line 5427 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 216:
#line 2028 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uvec2"; }
#line 5433 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 217:
#line 2029 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uvec3"; }
#line 5439 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 218:
#line 2030 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uvec4"; }
#line 5445 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 219:
#line 2031 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dvec2"; }
#line 5451 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 220:
#line 2032 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dvec3"; }
#line 5457 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 221:
#line 2033 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dvec4"; }
#line 5463 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 222:
#line 2034 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat2"; }
#line 5469 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 223:
#line 2035 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat2x3"; }
#line 5475 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 224:
#line 2036 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat2x4"; }
#line 5481 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 225:
#line 2037 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat3x2"; }
#line 5487 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 226:
#line 2038 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat3"; }
#line 5493 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 227:
#line 2039 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat3x4"; }
#line 5499 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 228:
#line 2040 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat4x2"; }
#line 5505 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 229:
#line 2041 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat4x3"; }
#line 5511 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 230:
#line 2042 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "mat4"; }
#line 5517 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 231:
#line 2043 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat2"; }
#line 5523 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 232:
#line 2044 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat2x3"; }
#line 5529 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 233:
#line 2045 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat2x4"; }
#line 5535 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 234:
#line 2046 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat3x2"; }
#line 5541 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 235:
#line 2047 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat3"; }
#line 5547 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 236:
#line 2048 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat3x4"; }
#line 5553 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 237:
#line 2049 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat4x2"; }
#line 5559 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 238:
#line 2050 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat4x3"; }
#line 5565 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 239:
#line 2051 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "dmat4"; }
#line 5571 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 240:
#line 2052 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1D"; }
#line 5577 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 241:
#line 2053 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2D"; }
#line 5583 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 242:
#line 2054 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DRect"; }
#line 5589 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 243:
#line 2055 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler3D"; }
#line 5595 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 244:
#line 2056 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCube"; }
#line 5601 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 245:
#line 2057 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerExternalOES"; }
#line 5607 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 246:
#line 2058 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1DShadow"; }
#line 5613 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 247:
#line 2059 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DShadow"; }
#line 5619 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 248:
#line 2060 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DRectShadow"; }
#line 5625 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 249:
#line 2061 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCubeShadow"; }
#line 5631 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 250:
#line 2062 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1DArray"; }
#line 5637 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 251:
#line 2063 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DArray"; }
#line 5643 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 252:
#line 2064 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler1DArrayShadow"; }
#line 5649 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 253:
#line 2065 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DArrayShadow"; }
#line 5655 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 254:
#line 2066 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerBuffer"; }
#line 5661 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 255:
#line 2067 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCubeArray"; }
#line 5667 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 256:
#line 2068 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "samplerCubeArrayShadow"; }
#line 5673 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 257:
#line 2069 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler1D"; }
#line 5679 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 258:
#line 2070 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2D"; }
#line 5685 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 259:
#line 2071 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DRect"; }
#line 5691 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 260:
#line 2072 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler3D"; }
#line 5697 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 261:
#line 2073 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isamplerCube"; }
#line 5703 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 262:
#line 2074 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler1DArray"; }
#line 5709 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 263:
#line 2075 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DArray"; }
#line 5715 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 264:
#line 2076 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isamplerBuffer"; }
#line 5721 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 265:
#line 2077 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isamplerCubeArray"; }
#line 5727 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 266:
#line 2078 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler1D"; }
#line 5733 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 267:
#line 2079 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2D"; }
#line 5739 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 268:
#line 2080 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DRect"; }
#line 5745 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 269:
#line 2081 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler3D"; }
#line 5751 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 270:
#line 2082 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usamplerCube"; }
#line 5757 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 271:
#line 2083 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler1DArray"; }
#line 5763 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 272:
#line 2084 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DArray"; }
#line 5769 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 273:
#line 2085 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usamplerBuffer"; }
#line 5775 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 274:
#line 2086 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usamplerCubeArray"; }
#line 5781 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 275:
#line 2087 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DMS"; }
#line 5787 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 276:
#line 2088 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DMS"; }
#line 5793 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 277:
#line 2089 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DMS"; }
#line 5799 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 278:
#line 2090 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "sampler2DMSArray"; }
#line 5805 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 279:
#line 2091 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "isampler2DMSArray"; }
#line 5811 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 280:
#line 2092 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "usampler2DMSArray"; }
#line 5817 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 281:
#line 2093 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image1D"; }
#line 5823 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 282:
#line 2094 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2D"; }
#line 5829 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 283:
#line 2095 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image3D"; }
#line 5835 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 284:
#line 2096 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DRect"; }
#line 5841 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 285:
#line 2097 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "imageCube"; }
#line 5847 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 286:
#line 2098 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "imageBuffer"; }
#line 5853 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 287:
#line 2099 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image1DArray"; }
#line 5859 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 288:
#line 2100 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DArray"; }
#line 5865 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 289:
#line 2101 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "imageCubeArray"; }
#line 5871 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 290:
#line 2102 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DMS"; }
#line 5877 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 291:
#line 2103 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "image2DMSArray"; }
#line 5883 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 292:
#line 2104 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage1D"; }
#line 5889 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 293:
#line 2105 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2D"; }
#line 5895 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 294:
#line 2106 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage3D"; }
#line 5901 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 295:
#line 2107 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DRect"; }
#line 5907 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 296:
#line 2108 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimageCube"; }
#line 5913 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 297:
#line 2109 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimageBuffer"; }
#line 5919 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 298:
#line 2110 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage1DArray"; }
#line 5925 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 299:
#line 2111 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DArray"; }
#line 5931 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 300:
#line 2112 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimageCubeArray"; }
#line 5937 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 301:
#line 2113 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DMS"; }
#line 5943 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 302:
#line 2114 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "iimage2DMSArray"; }
#line 5949 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 303:
#line 2115 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage1D"; }
#line 5955 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 304:
#line 2116 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2D"; }
#line 5961 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 305:
#line 2117 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage3D"; }
#line 5967 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 306:
#line 2118 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DRect"; }
#line 5973 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 307:
#line 2119 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimageCube"; }
#line 5979 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 308:
#line 2120 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimageBuffer"; }
#line 5985 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 309:
#line 2121 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage1DArray"; }
#line 5991 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 310:
#line 2122 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DArray"; }
#line 5997 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 311:
#line 2123 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimageCubeArray"; }
#line 6003 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 312:
#line 2124 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DMS"; }
#line 6009 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 313:
#line 2125 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "uimage2DMSArray"; }
#line 6015 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 314:
#line 2126 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.identifier) = "atomic_uint"; }
#line 6021 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 315:
#line 2131 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_high;
   }
#line 6030 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 316:
#line 2136 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_medium;
   }
#line 6039 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 317:
#line 2141 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->check_precision_qualifiers_allowed(&(yylsp[0]));
      (yyval.n) = ast_precision_low;
   }
#line 6048 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 318:
#line 2149 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.struct_specifier) = new(ctx) ast_struct_specifier((yyvsp[-3].identifier), (yyvsp[-1].declarator_list));
      (yyval.struct_specifier)->set_location_range((yylsp[-3]), (yylsp[0]));
      state->symbols->add_type((yyvsp[-3].identifier), glsl_type::void_type);
   }
#line 6059 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 319:
#line 2156 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.struct_specifier) = new(ctx) ast_struct_specifier(NULL, (yyvsp[-1].declarator_list));
      (yyval.struct_specifier)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 6069 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 320:
#line 2165 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[0].declarator_list);
      (yyvsp[0].declarator_list)->link.self_link();
   }
#line 6078 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 321:
#line 2170 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[-1].declarator_list);
      (yyval.declarator_list)->link.insert_before(& (yyvsp[0].declarator_list)->link);
   }
#line 6087 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 322:
#line 2178 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_fully_specified_type *const type = (yyvsp[-2].fully_specified_type);
      type->set_location((yylsp[-2]));

      if (type->qualifier.flags.i != 0)
         _mesa_glsl_error(&(yylsp[-2]), state,
			  "only precision qualifiers may be applied to "
			  "structure members");

      (yyval.declarator_list) = new(ctx) ast_declarator_list(type);
      (yyval.declarator_list)->set_location((yylsp[-1]));

      (yyval.declarator_list)->declarations.push_degenerate_list_at_head(& (yyvsp[-1].declaration)->link);
   }
#line 6107 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 323:
#line 2197 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declaration) = (yyvsp[0].declaration);
      (yyvsp[0].declaration)->link.self_link();
   }
#line 6116 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 324:
#line 2202 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declaration) = (yyvsp[-2].declaration);
      (yyval.declaration)->link.insert_before(& (yyvsp[0].declaration)->link);
   }
#line 6125 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 325:
#line 2210 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.declaration) = new(ctx) ast_declaration((yyvsp[0].identifier), NULL, NULL);
      (yyval.declaration)->set_location((yylsp[0]));
   }
#line 6135 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 326:
#line 2216 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.declaration) = new(ctx) ast_declaration((yyvsp[-1].identifier), (yyvsp[0].array_specifier), NULL);
      (yyval.declaration)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6145 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 328:
#line 2226 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-1].expression);
   }
#line 6153 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 329:
#line 2230 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.expression) = (yyvsp[-2].expression);
   }
#line 6161 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 330:
#line 2237 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.expression) = new(ctx) ast_aggregate_initializer();
      (yyval.expression)->set_location((yylsp[0]));
      (yyval.expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 6172 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 331:
#line 2244 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyvsp[-2].expression)->expressions.push_tail(& (yyvsp[0].expression)->link);
   }
#line 6180 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 333:
#line 2256 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (ast_node *) (yyvsp[0].compound_statement); }
#line 6186 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 341:
#line 2271 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(true, NULL);
      (yyval.compound_statement)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6196 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 342:
#line 2277 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      state->symbols->push_scope();
   }
#line 6204 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 343:
#line 2281 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(true, (yyvsp[-1].node));
      (yyval.compound_statement)->set_location_range((yylsp[-3]), (yylsp[0]));
      state->symbols->pop_scope();
   }
#line 6215 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 344:
#line 2290 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (ast_node *) (yyvsp[0].compound_statement); }
#line 6221 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 346:
#line 2296 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(false, NULL);
      (yyval.compound_statement)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6231 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 347:
#line 2302 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.compound_statement) = new(ctx) ast_compound_statement(false, (yyvsp[-1].node));
      (yyval.compound_statement)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 6241 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 348:
#line 2311 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].node) == NULL) {
         _mesa_glsl_error(& (yylsp[0]), state, "<nil> statement");
         assert((yyvsp[0].node) != NULL);
      }

      (yyval.node) = (yyvsp[0].node);
      (yyval.node)->link.self_link();
   }
#line 6255 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 349:
#line 2321 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if ((yyvsp[0].node) == NULL) {
         _mesa_glsl_error(& (yylsp[0]), state, "<nil> statement");
         assert((yyvsp[0].node) != NULL);
      }
      (yyval.node) = (yyvsp[-1].node);
      (yyval.node)->link.insert_before(& (yyvsp[0].node)->link);
   }
#line 6268 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 350:
#line 2333 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_expression_statement(NULL);
      (yyval.node)->set_location((yylsp[0]));
   }
#line 6278 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 351:
#line 2339 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_expression_statement((yyvsp[-1].expression));
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6288 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 352:
#line 2348 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = new(state) ast_selection_statement((yyvsp[-2].expression), (yyvsp[0].selection_rest_statement).then_statement,
                                              (yyvsp[0].selection_rest_statement).else_statement);
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 6298 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 353:
#line 2357 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.selection_rest_statement).then_statement = (yyvsp[-2].node);
      (yyval.selection_rest_statement).else_statement = (yyvsp[0].node);
   }
#line 6307 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 354:
#line 2362 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.selection_rest_statement).then_statement = (yyvsp[0].node);
      (yyval.selection_rest_statement).else_statement = NULL;
   }
#line 6316 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 355:
#line 2370 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (ast_node *) (yyvsp[0].expression);
   }
#line 6324 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 356:
#line 2374 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      ast_declaration *decl = new(ctx) ast_declaration((yyvsp[-2].identifier), NULL, (yyvsp[0].expression));
      ast_declarator_list *declarator = new(ctx) ast_declarator_list((yyvsp[-3].fully_specified_type));
      decl->set_location_range((yylsp[-2]), (yylsp[0]));
      declarator->set_location((yylsp[-3]));

      declarator->declarations.push_tail(&decl->link);
      (yyval.node) = declarator;
   }
#line 6339 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 357:
#line 2392 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = new(state) ast_switch_statement((yyvsp[-2].expression), (yyvsp[0].switch_body));
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[0]));
   }
#line 6348 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 358:
#line 2400 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.switch_body) = new(state) ast_switch_body(NULL);
      (yyval.switch_body)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6357 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 359:
#line 2405 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.switch_body) = new(state) ast_switch_body((yyvsp[-1].case_statement_list));
      (yyval.switch_body)->set_location_range((yylsp[-2]), (yylsp[0]));
   }
#line 6366 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 360:
#line 2413 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label) = new(state) ast_case_label((yyvsp[-1].expression));
      (yyval.case_label)->set_location((yylsp[-1]));
   }
#line 6375 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 361:
#line 2418 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label) = new(state) ast_case_label(NULL);
      (yyval.case_label)->set_location((yylsp[0]));
   }
#line 6384 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 362:
#line 2426 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_label_list *labels = new(state) ast_case_label_list();

      labels->labels.push_tail(& (yyvsp[0].case_label)->link);
      (yyval.case_label_list) = labels;
      (yyval.case_label_list)->set_location((yylsp[0]));
   }
#line 6396 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 363:
#line 2434 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_label_list) = (yyvsp[-1].case_label_list);
      (yyval.case_label_list)->labels.push_tail(& (yyvsp[0].case_label)->link);
   }
#line 6405 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 364:
#line 2442 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_statement *stmts = new(state) ast_case_statement((yyvsp[-1].case_label_list));
      stmts->set_location((yylsp[0]));

      stmts->stmts.push_tail(& (yyvsp[0].node)->link);
      (yyval.case_statement) = stmts;
   }
#line 6417 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 365:
#line 2450 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_statement) = (yyvsp[-1].case_statement);
      (yyval.case_statement)->stmts.push_tail(& (yyvsp[0].node)->link);
   }
#line 6426 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 366:
#line 2458 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_case_statement_list *cases= new(state) ast_case_statement_list();
      cases->set_location((yylsp[0]));

      cases->cases.push_tail(& (yyvsp[0].case_statement)->link);
      (yyval.case_statement_list) = cases;
   }
#line 6438 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 367:
#line 2466 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.case_statement_list) = (yyvsp[-1].case_statement_list);
      (yyval.case_statement_list)->cases.push_tail(& (yyvsp[0].case_statement)->link);
   }
#line 6447 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 368:
#line 2474 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_while,
                                            NULL, (yyvsp[-2].node), NULL, (yyvsp[0].node));
      (yyval.node)->set_location_range((yylsp[-4]), (yylsp[-1]));
   }
#line 6458 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 369:
#line 2481 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_do_while,
                                            NULL, (yyvsp[-2].expression), NULL, (yyvsp[-5].node));
      (yyval.node)->set_location_range((yylsp[-6]), (yylsp[-1]));
   }
#line 6469 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 370:
#line 2488 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_iteration_statement(ast_iteration_statement::ast_for,
                                            (yyvsp[-3].node), (yyvsp[-2].for_rest_statement).cond, (yyvsp[-2].for_rest_statement).rest, (yyvsp[0].node));
      (yyval.node)->set_location_range((yylsp[-5]), (yylsp[0]));
   }
#line 6480 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 374:
#line 2504 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
   }
#line 6488 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 375:
#line 2511 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.for_rest_statement).cond = (yyvsp[-1].node);
      (yyval.for_rest_statement).rest = NULL;
   }
#line 6497 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 376:
#line 2516 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.for_rest_statement).cond = (yyvsp[-2].node);
      (yyval.for_rest_statement).rest = (yyvsp[0].expression);
   }
#line 6506 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 377:
#line 2525 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_continue, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6516 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 378:
#line 2531 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_break, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6526 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 379:
#line 2537 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_return, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6536 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 380:
#line 2543 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_return, (yyvsp[-1].expression));
      (yyval.node)->set_location_range((yylsp[-2]), (yylsp[-1]));
   }
#line 6546 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 381:
#line 2549 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.node) = new(ctx) ast_jump_statement(ast_jump_statement::ast_discard, NULL);
      (yyval.node)->set_location((yylsp[-1]));
   }
#line 6556 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 382:
#line 2557 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].function_definition); }
#line 6562 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 383:
#line 2558 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].node); }
#line 6568 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 384:
#line 2559 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = NULL; }
#line 6574 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 385:
#line 2560 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    { (yyval.node) = (yyvsp[0].node); }
#line 6580 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 386:
#line 2565 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
      (yyval.function_definition) = new(ctx) ast_function_definition();
      (yyval.function_definition)->set_location_range((yylsp[-1]), (yylsp[0]));
      (yyval.function_definition)->prototype = (yyvsp[-1].function);
      (yyval.function_definition)->body = (yyvsp[0].compound_statement);

      state->symbols->pop_scope();
   }
#line 6594 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 387:
#line 2579 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = (yyvsp[0].interface_block);
   }
#line 6602 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 388:
#line 2583 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *block = (ast_interface_block *) (yyvsp[0].node);

      if (!state->has_420pack_or_es31() && block->layout.has_layout() &&
          !block->layout.is_default_qualifier) {
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate layout(...) qualifiers");
         YYERROR;
      }

      if (!block->layout.merge_qualifier(& (yylsp[-1]), state, (yyvsp[-1].type_qualifier), false)) {
         YYERROR;
      }

      block->layout.is_default_qualifier = false;

      (yyval.node) = block;
   }
#line 6624 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 389:
#line 2601 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *block = (ast_interface_block *)(yyvsp[0].node);

      if (!block->layout.flags.q.buffer) {
            _mesa_glsl_error(& (yylsp[-1]), state,
                             "memory qualifiers can only be used in the "
                             "declaration of shader storage blocks");
      }
      if (!block->layout.merge_qualifier(& (yylsp[-1]), state, (yyvsp[-1].type_qualifier), false)) {
         YYERROR;
      }
      (yyval.node) = block;
   }
#line 6642 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 390:
#line 2618 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *const block = (yyvsp[-1].interface_block);

      block->block_name = (yyvsp[-5].identifier);
      block->declarations.push_degenerate_list_at_head(& (yyvsp[-3].declarator_list)->link);

      _mesa_ast_process_interface_block(& (yylsp[-6]), state, block, (yyvsp[-6].type_qualifier));

      (yyval.interface_block) = block;
   }
#line 6657 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 391:
#line 2629 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      ast_interface_block *const block = (yyvsp[-1].interface_block);

      block->block_name = (yyvsp[-5].identifier);
      block->declarations.push_degenerate_list_at_head(& (yyvsp[-3].declarator_list)->link);

      _mesa_ast_process_interface_block(& (yylsp[-6]), state, block, (yyvsp[-6].type_qualifier));

      (yyval.interface_block) = block;
   }
#line 6672 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 392:
#line 2643 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.in = 1;
   }
#line 6681 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 393:
#line 2648 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.out = 1;
   }
#line 6690 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 394:
#line 2653 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.uniform = 1;
   }
#line 6699 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 395:
#line 2661 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      memset(& (yyval.type_qualifier), 0, sizeof((yyval.type_qualifier)));
      (yyval.type_qualifier).flags.q.buffer = 1;
   }
#line 6708 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 396:
#line 2669 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state) ast_interface_block(*state->default_uniform_qualifier,
                                          NULL, NULL);
   }
#line 6717 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 397:
#line 2674 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state) ast_interface_block(*state->default_uniform_qualifier,
                                          (yyvsp[0].identifier), NULL);
      (yyval.interface_block)->set_location((yylsp[0]));
   }
#line 6727 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 398:
#line 2680 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state) ast_interface_block(*state->default_uniform_qualifier,
                                          (yyvsp[-1].identifier), (yyvsp[0].array_specifier));
      (yyval.interface_block)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6737 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 399:
#line 2689 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state) ast_interface_block(*state->default_shader_storage_qualifier,
                                          NULL, NULL);
   }
#line 6746 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 400:
#line 2694 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state) ast_interface_block(*state->default_shader_storage_qualifier,
                                          (yyvsp[0].identifier), NULL);
      (yyval.interface_block)->set_location((yylsp[0]));
   }
#line 6756 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 401:
#line 2700 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.interface_block) = new(state) ast_interface_block(*state->default_shader_storage_qualifier,
                                          (yyvsp[-1].identifier), (yyvsp[0].array_specifier));
      (yyval.interface_block)->set_location_range((yylsp[-1]), (yylsp[0]));
   }
#line 6766 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 402:
#line 2709 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[0].declarator_list);
      (yyvsp[0].declarator_list)->link.self_link();
   }
#line 6775 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 403:
#line 2714 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.declarator_list) = (yyvsp[-1].declarator_list);
      (yyvsp[0].declarator_list)->link.insert_before(& (yyval.declarator_list)->link);
   }
#line 6784 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 404:
#line 2722 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      void *ctx = state;
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
#line 6809 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 405:
#line 2746 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->has_420pack_or_es31()) {
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate layout(...) qualifiers");
         YYERROR;
      } else {
         if (!state->default_uniform_qualifier->
                merge_qualifier(& (yylsp[-1]), state, (yyvsp[-1].type_qualifier), false)) {
            YYERROR;
         }
      }
   }
#line 6826 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 406:
#line 2759 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!state->default_uniform_qualifier->
             merge_qualifier(& (yylsp[-2]), state, (yyvsp[-2].type_qualifier), false)) {
         YYERROR;
      }
      (yyval.node) = NULL;
   }
#line 6838 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 407:
#line 2770 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->has_420pack_or_es31()) {
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate layout(...) qualifiers");
         YYERROR;
      } else {
         if (!state->default_shader_storage_qualifier->
                merge_qualifier(& (yylsp[-1]), state, (yyvsp[-1].type_qualifier), false)) {
            YYERROR;
         }
      }
   }
#line 6855 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 408:
#line 2783 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      if (!state->default_shader_storage_qualifier->
             merge_qualifier(& (yylsp[-2]), state, (yyvsp[-2].type_qualifier), false)) {
         YYERROR;
      }

      /* From the GLSL 4.50 spec, section 4.4.5:
       *
       *     "It is a compile-time error to specify the binding identifier for
       *     the global scope or for block member declarations."
       */
      if (state->default_shader_storage_qualifier->flags.q.explicit_binding) {
         _mesa_glsl_error(& (yylsp[-2]), state,
                          "binding qualifier cannot be set for default layout");
      }

      (yyval.node) = NULL;
   }
#line 6878 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 409:
#line 2805 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->has_420pack_or_es31()) {
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate layout(...) qualifiers");
         YYERROR;
      } else {
         if (!state->in_qualifier->
                merge_in_qualifier(& (yylsp[-1]), state, (yyvsp[-1].type_qualifier), (yyval.node), false)) {
            YYERROR;
         }
      }
   }
#line 6895 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 410:
#line 2818 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->in_qualifier->
             merge_in_qualifier(& (yylsp[-2]), state, (yyvsp[-2].type_qualifier), (yyval.node), true)) {
         YYERROR;
      }
   }
#line 6907 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 411:
#line 2829 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->has_420pack_or_es31()) {
         _mesa_glsl_error(&(yylsp[-1]), state, "duplicate layout(...) qualifiers");
         YYERROR;
      } else {
         if (!state->out_qualifier->
                merge_out_qualifier(& (yylsp[-1]), state, (yyvsp[-1].type_qualifier), (yyval.node), false)) {
            YYERROR;
         }
      }
   }
#line 6924 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;

  case 412:
#line 2842 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1646  */
    {
      (yyval.node) = NULL;
      if (!state->out_qualifier->
             merge_out_qualifier(& (yylsp[-2]), state, (yyvsp[-2].type_qualifier), (yyval.node), true))
         YYERROR;
   }
#line 6935 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
    break;


#line 6939 "glsl/glsl_parser.cpp" /* yacc.c:1646  */
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
