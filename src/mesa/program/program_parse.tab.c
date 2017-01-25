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
#define yyparse         _mesa_program_parse
#define yylex           _mesa_program_lex
#define yyerror         _mesa_program_error
#define yydebug         _mesa_program_debug
#define yynerrs         _mesa_program_nerrs


/* Copy the first part of user declarations.  */
#line 1 "./program/program_parse.y" /* yacc.c:339  */

/*
 * Copyright © 2009 Intel Corporation
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main/mtypes.h"
#include "main/imports.h"
#include "program/program.h"
#include "program/prog_parameter.h"
#include "program/prog_parameter_layout.h"
#include "program/prog_statevars.h"
#include "program/prog_instruction.h"

#include "program/symbol_table.h"
#include "program/program_parser.h"

extern void *yy_scan_string(char *);
extern void yy_delete_buffer(void *);

static struct asm_symbol *declare_variable(struct asm_parser_state *state,
    char *name, enum asm_type t, struct YYLTYPE *locp);

static int add_state_reference(struct gl_program_parameter_list *param_list,
    const gl_state_index tokens[STATE_LENGTH]);

static int initialize_symbol_from_state(struct gl_program *prog,
    struct asm_symbol *param_var, const gl_state_index tokens[STATE_LENGTH]);

static int initialize_symbol_from_param(struct gl_program *prog,
    struct asm_symbol *param_var, const gl_state_index tokens[STATE_LENGTH]);

static int initialize_symbol_from_const(struct gl_program *prog,
    struct asm_symbol *param_var, const struct asm_vector *vec,
    GLboolean allowSwizzle);

static int yyparse(struct asm_parser_state *state);

static char *make_error_string(const char *fmt, ...);

static void yyerror(struct YYLTYPE *locp, struct asm_parser_state *state,
    const char *s);

static int validate_inputs(struct YYLTYPE *locp,
    struct asm_parser_state *state);

static void init_dst_reg(struct prog_dst_register *r);

static void set_dst_reg(struct prog_dst_register *r,
                        gl_register_file file, GLint index);

static void init_src_reg(struct asm_src_register *r);

static void set_src_reg(struct asm_src_register *r,
                        gl_register_file file, GLint index);

static void set_src_reg_swz(struct asm_src_register *r,
                            gl_register_file file, GLint index, GLuint swizzle);

static void asm_instruction_set_operands(struct asm_instruction *inst,
    const struct prog_dst_register *dst, const struct asm_src_register *src0,
    const struct asm_src_register *src1, const struct asm_src_register *src2);

static struct asm_instruction *asm_instruction_ctor(enum prog_opcode op,
    const struct prog_dst_register *dst, const struct asm_src_register *src0,
    const struct asm_src_register *src1, const struct asm_src_register *src2);

static struct asm_instruction *asm_instruction_copy_ctor(
    const struct prog_instruction *base, const struct prog_dst_register *dst,
    const struct asm_src_register *src0, const struct asm_src_register *src1,
    const struct asm_src_register *src2);

#ifndef FALSE
#define FALSE 0
#define TRUE (!FALSE)
#endif

#define YYLLOC_DEFAULT(Current, Rhs, N)					\
   do {									\
      if (N) {							\
	 (Current).first_line = YYRHSLOC(Rhs, 1).first_line;		\
	 (Current).first_column = YYRHSLOC(Rhs, 1).first_column;	\
	 (Current).position = YYRHSLOC(Rhs, 1).position;		\
	 (Current).last_line = YYRHSLOC(Rhs, N).last_line;		\
	 (Current).last_column = YYRHSLOC(Rhs, N).last_column;		\
      } else {								\
	 (Current).first_line = YYRHSLOC(Rhs, 0).last_line;		\
	 (Current).last_line = (Current).first_line;			\
	 (Current).first_column = YYRHSLOC(Rhs, 0).last_column;		\
	 (Current).last_column = (Current).first_column;		\
	 (Current).position = YYRHSLOC(Rhs, 0).position			\
	    + (Current).first_column;					\
      }									\
   } while(0)

#line 191 "program/program_parse.tab.c" /* yacc.c:339  */

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
   by #include "program_parse.tab.h".  */
#ifndef YY__MESA_PROGRAM_PROGRAM_PROGRAM_PARSE_TAB_H_INCLUDED
# define YY__MESA_PROGRAM_PROGRAM_PROGRAM_PARSE_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int _mesa_program_debug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    ARBvp_10 = 258,
    ARBfp_10 = 259,
    ADDRESS = 260,
    ALIAS = 261,
    ATTRIB = 262,
    OPTION = 263,
    OUTPUT = 264,
    PARAM = 265,
    TEMP = 266,
    END = 267,
    BIN_OP = 268,
    BINSC_OP = 269,
    SAMPLE_OP = 270,
    SCALAR_OP = 271,
    TRI_OP = 272,
    VECTOR_OP = 273,
    ARL = 274,
    KIL = 275,
    SWZ = 276,
    TXD_OP = 277,
    INTEGER = 278,
    REAL = 279,
    AMBIENT = 280,
    ATTENUATION = 281,
    BACK = 282,
    CLIP = 283,
    COLOR = 284,
    DEPTH = 285,
    DIFFUSE = 286,
    DIRECTION = 287,
    EMISSION = 288,
    ENV = 289,
    EYE = 290,
    FOG = 291,
    FOGCOORD = 292,
    FRAGMENT = 293,
    FRONT = 294,
    HALF = 295,
    INVERSE = 296,
    INVTRANS = 297,
    LIGHT = 298,
    LIGHTMODEL = 299,
    LIGHTPROD = 300,
    LOCAL = 301,
    MATERIAL = 302,
    MAT_PROGRAM = 303,
    MATRIX = 304,
    MATRIXINDEX = 305,
    MODELVIEW = 306,
    MVP = 307,
    NORMAL = 308,
    OBJECT = 309,
    PALETTE = 310,
    PARAMS = 311,
    PLANE = 312,
    POINT_TOK = 313,
    POINTSIZE = 314,
    POSITION = 315,
    PRIMARY = 316,
    PROGRAM = 317,
    PROJECTION = 318,
    RANGE = 319,
    RESULT = 320,
    ROW = 321,
    SCENECOLOR = 322,
    SECONDARY = 323,
    SHININESS = 324,
    SIZE_TOK = 325,
    SPECULAR = 326,
    SPOT = 327,
    STATE = 328,
    TEXCOORD = 329,
    TEXENV = 330,
    TEXGEN = 331,
    TEXGEN_Q = 332,
    TEXGEN_R = 333,
    TEXGEN_S = 334,
    TEXGEN_T = 335,
    TEXTURE = 336,
    TRANSPOSE = 337,
    TEXTURE_UNIT = 338,
    TEX_1D = 339,
    TEX_2D = 340,
    TEX_3D = 341,
    TEX_CUBE = 342,
    TEX_RECT = 343,
    TEX_SHADOW1D = 344,
    TEX_SHADOW2D = 345,
    TEX_SHADOWRECT = 346,
    TEX_ARRAY1D = 347,
    TEX_ARRAY2D = 348,
    TEX_ARRAYSHADOW1D = 349,
    TEX_ARRAYSHADOW2D = 350,
    VERTEX = 351,
    VTXATTRIB = 352,
    WEIGHT = 353,
    IDENTIFIER = 354,
    USED_IDENTIFIER = 355,
    MASK4 = 356,
    MASK3 = 357,
    MASK2 = 358,
    MASK1 = 359,
    SWIZZLE = 360,
    DOT_DOT = 361,
    DOT = 362
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 126 "./program/program_parse.y" /* yacc.c:355  */

   struct asm_instruction *inst;
   struct asm_symbol *sym;
   struct asm_symbol temp_sym;
   struct asm_swizzle_mask swiz_mask;
   struct asm_src_register src_reg;
   struct prog_dst_register dst_reg;
   struct prog_instruction temp_inst;
   char *string;
   unsigned result;
   unsigned attrib;
   int integer;
   float real;
   gl_state_index state[STATE_LENGTH];
   int negate;
   struct asm_vector vector;
   enum prog_opcode opcode;

   struct {
      unsigned swz;
      unsigned rgba_valid:1;
      unsigned xyzw_valid:1;
      unsigned negate:1;
   } ext_swizzle;

#line 365 "program/program_parse.tab.c" /* yacc.c:355  */
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



int _mesa_program_parse (struct asm_parser_state *state);

#endif /* !YY__MESA_PROGRAM_PROGRAM_PROGRAM_PARSE_TAB_H_INCLUDED  */

/* Copy the second part of user declarations.  */
#line 269 "./program/program_parse.y" /* yacc.c:358  */

extern int
_mesa_program_lexer_lex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param,
                        void *yyscanner);

static int
yylex(YYSTYPE *yylval_param, YYLTYPE *yylloc_param,
      struct asm_parser_state *state)
{
   return _mesa_program_lexer_lex(yylval_param, yylloc_param, state->scanner);
}

#line 407 "program/program_parse.tab.c" /* yacc.c:358  */

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
#define YYLAST   357

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  117
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  137
/* YYNRULES -- Number of rules.  */
#define YYNRULES  270
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  455

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   362

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   112,   109,   113,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,   108,
       2,   114,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,   110,     2,   111,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   115,     2,   116,     2,     2,     2,     2,
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
     105,   106,   107
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   284,   284,   287,   295,   307,   308,   311,   335,   336,
     339,   354,   357,   362,   369,   370,   371,   372,   373,   374,
     375,   378,   379,   380,   383,   389,   395,   401,   408,   414,
     421,   465,   472,   516,   522,   523,   524,   525,   526,   527,
     528,   529,   530,   531,   532,   533,   536,   548,   558,   567,
     580,   602,   609,   642,   649,   665,   724,   767,   776,   798,
     808,   812,   841,   860,   860,   862,   869,   881,   882,   883,
     886,   900,   914,   934,   945,   957,   959,   960,   961,   962,
     965,   965,   965,   965,   966,   969,   970,   971,   972,   973,
     974,   977,   996,  1000,  1006,  1010,  1014,  1018,  1022,  1026,
    1030,  1035,  1041,  1052,  1052,  1053,  1055,  1059,  1063,  1067,
    1073,  1073,  1075,  1093,  1119,  1122,  1137,  1143,  1149,  1150,
    1157,  1163,  1169,  1177,  1183,  1189,  1197,  1203,  1209,  1217,
    1218,  1221,  1222,  1223,  1224,  1225,  1226,  1227,  1228,  1229,
    1230,  1231,  1234,  1243,  1247,  1251,  1257,  1266,  1270,  1274,
    1283,  1287,  1293,  1299,  1306,  1311,  1319,  1329,  1331,  1339,
    1345,  1349,  1353,  1359,  1370,  1379,  1383,  1388,  1392,  1396,
    1400,  1406,  1413,  1417,  1423,  1431,  1442,  1449,  1453,  1459,
    1469,  1480,  1484,  1502,  1511,  1514,  1520,  1524,  1528,  1534,
    1545,  1550,  1555,  1560,  1565,  1570,  1578,  1581,  1586,  1599,
    1607,  1618,  1626,  1626,  1628,  1628,  1630,  1640,  1645,  1652,
    1662,  1671,  1676,  1683,  1693,  1703,  1715,  1715,  1716,  1716,
    1718,  1728,  1736,  1746,  1754,  1762,  1771,  1782,  1786,  1792,
    1793,  1794,  1797,  1797,  1800,  1800,  1803,  1810,  1819,  1833,
    1842,  1851,  1855,  1864,  1873,  1884,  1891,  1901,  1929,  1938,
    1950,  1953,  1962,  1973,  1974,  1975,  1978,  1979,  1980,  1983,
    1984,  1987,  1988,  1991,  1992,  1995,  2006,  2017,  2028,  2054,
    2055
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 1
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ARBvp_10", "ARBfp_10", "ADDRESS",
  "ALIAS", "ATTRIB", "OPTION", "OUTPUT", "PARAM", "TEMP", "END", "BIN_OP",
  "BINSC_OP", "SAMPLE_OP", "SCALAR_OP", "TRI_OP", "VECTOR_OP", "ARL",
  "KIL", "SWZ", "TXD_OP", "INTEGER", "REAL", "AMBIENT", "ATTENUATION",
  "BACK", "CLIP", "COLOR", "DEPTH", "DIFFUSE", "DIRECTION", "EMISSION",
  "ENV", "EYE", "FOG", "FOGCOORD", "FRAGMENT", "FRONT", "HALF", "INVERSE",
  "INVTRANS", "LIGHT", "LIGHTMODEL", "LIGHTPROD", "LOCAL", "MATERIAL",
  "MAT_PROGRAM", "MATRIX", "MATRIXINDEX", "MODELVIEW", "MVP", "NORMAL",
  "OBJECT", "PALETTE", "PARAMS", "PLANE", "POINT_TOK", "POINTSIZE",
  "POSITION", "PRIMARY", "PROGRAM", "PROJECTION", "RANGE", "RESULT", "ROW",
  "SCENECOLOR", "SECONDARY", "SHININESS", "SIZE_TOK", "SPECULAR", "SPOT",
  "STATE", "TEXCOORD", "TEXENV", "TEXGEN", "TEXGEN_Q", "TEXGEN_R",
  "TEXGEN_S", "TEXGEN_T", "TEXTURE", "TRANSPOSE", "TEXTURE_UNIT", "TEX_1D",
  "TEX_2D", "TEX_3D", "TEX_CUBE", "TEX_RECT", "TEX_SHADOW1D",
  "TEX_SHADOW2D", "TEX_SHADOWRECT", "TEX_ARRAY1D", "TEX_ARRAY2D",
  "TEX_ARRAYSHADOW1D", "TEX_ARRAYSHADOW2D", "VERTEX", "VTXATTRIB",
  "WEIGHT", "IDENTIFIER", "USED_IDENTIFIER", "MASK4", "MASK3", "MASK2",
  "MASK1", "SWIZZLE", "DOT_DOT", "DOT", "';'", "','", "'['", "']'", "'+'",
  "'-'", "'='", "'{'", "'}'", "$accept", "program", "language",
  "optionSequence", "option", "statementSequence", "statement",
  "instruction", "ALU_instruction", "TexInstruction", "ARL_instruction",
  "VECTORop_instruction", "SCALARop_instruction", "BINSCop_instruction",
  "BINop_instruction", "TRIop_instruction", "SAMPLE_instruction",
  "KIL_instruction", "TXD_instruction", "texImageUnit", "texTarget",
  "SWZ_instruction", "scalarSrcReg", "scalarUse", "swizzleSrcReg",
  "maskedDstReg", "maskedAddrReg", "extendedSwizzle", "extSwizComp",
  "extSwizSel", "srcReg", "dstReg", "progParamArray", "progParamArrayMem",
  "progParamArrayAbs", "progParamArrayRel", "addrRegRelOffset",
  "addrRegPosOffset", "addrRegNegOffset", "addrReg", "addrComponent",
  "addrWriteMask", "scalarSuffix", "swizzleSuffix", "optionalMask",
  "namingStatement", "ATTRIB_statement", "attribBinding", "vtxAttribItem",
  "vtxAttribNum", "vtxOptWeightNum", "vtxWeightNum", "fragAttribItem",
  "PARAM_statement", "PARAM_singleStmt", "PARAM_multipleStmt",
  "optArraySize", "paramSingleInit", "paramMultipleInit",
  "paramMultInitList", "paramSingleItemDecl", "paramSingleItemUse",
  "paramMultipleItem", "stateMultipleItem", "stateSingleItem",
  "stateMaterialItem", "stateMatProperty", "stateLightItem",
  "stateLightProperty", "stateSpotProperty", "stateLightModelItem",
  "stateLModProperty", "stateLightProdItem", "stateLProdProperty",
  "stateTexEnvItem", "stateTexEnvProperty", "ambDiffSpecProperty",
  "stateLightNumber", "stateTexGenItem", "stateTexGenType",
  "stateTexGenCoord", "stateFogItem", "stateFogProperty",
  "stateClipPlaneItem", "stateClipPlaneNum", "statePointItem",
  "statePointProperty", "stateMatrixRow", "stateMatrixRows",
  "optMatrixRows", "stateMatrixItem", "stateOptMatModifier",
  "stateMatModifier", "stateMatrixRowNum", "stateMatrixName",
  "stateOptModMatNum", "stateModMatNum", "statePaletteMatNum",
  "stateProgramMatNum", "stateDepthItem", "programSingleItem",
  "programMultipleItem", "progEnvParams", "progEnvParamNums",
  "progEnvParam", "progLocalParams", "progLocalParamNums",
  "progLocalParam", "progEnvParamNum", "progLocalParamNum",
  "paramConstDecl", "paramConstUse", "paramConstScalarDecl",
  "paramConstScalarUse", "paramConstVector", "signedFloatConstant",
  "optionalSign", "TEMP_statement", "@1", "ADDRESS_statement", "@2",
  "varNameList", "OUTPUT_statement", "resultBinding", "resultColBinding",
  "optResultFaceType", "optResultColorType", "optFaceType", "optColorType",
  "optTexCoordUnitNum", "optTexImageUnitNum", "optLegacyTexUnitNum",
  "texCoordUnitNum", "texImageUnitNum", "legacyTexUnitNum",
  "ALIAS_statement", "string", YY_NULLPTR
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
     355,   356,   357,   358,   359,   360,   361,   362,    59,    44,
      91,    93,    43,    45,    61,   123,   125
};
# endif

#define YYPACT_NINF -399

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-399)))

#define YYTABLE_NINF -63

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      25,  -399,  -399,    60,  -399,  -399,    41,    -7,  -399,   174,
    -399,  -399,   -34,  -399,   -23,   -17,    11,    33,  -399,  -399,
     -30,   -30,   -30,   -30,   -30,   -30,     9,    49,   -30,   -30,
    -399,    32,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
    -399,  -399,  -399,  -399,    51,  -399,  -399,  -399,  -399,  -399,
    -399,  -399,  -399,  -399,    44,    53,   112,   114,    62,    44,
     104,  -399,   118,   117,  -399,   120,   121,   122,   123,   124,
    -399,   125,    14,  -399,  -399,  -399,   -16,   126,   127,  -399,
    -399,  -399,   128,   138,   -15,   175,   216,   -26,  -399,   128,
     -12,  -399,  -399,  -399,  -399,   136,  -399,    49,  -399,  -399,
    -399,  -399,  -399,    49,    49,    49,    49,    49,    49,  -399,
    -399,  -399,  -399,     4,    21,    78,     3,   137,    49,    65,
     139,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
     -16,    49,   149,  -399,  -399,  -399,  -399,   140,  -399,  -399,
    -399,  -399,  -399,  -399,  -399,   150,  -399,  -399,   227,    52,
     229,  -399,   144,   146,   -16,   147,  -399,   148,  -399,  -399,
      69,  -399,  -399,   136,  -399,   151,   152,   153,   190,    10,
     154,    46,   155,    76,    87,    -1,   156,   136,  -399,  -399,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,   192,  -399,
      69,  -399,   157,  -399,  -399,   136,   158,   159,  -399,    35,
    -399,  -399,  -399,  -399,    -3,   162,   163,  -399,   160,  -399,
    -399,   165,  -399,  -399,  -399,  -399,   166,    49,    49,  -399,
     176,   177,    49,  -399,  -399,  -399,  -399,   236,   255,   256,
    -399,  -399,  -399,  -399,   258,  -399,  -399,  -399,  -399,   215,
     258,    19,   173,   178,  -399,   179,  -399,   136,    20,  -399,
    -399,  -399,   261,   257,    -9,   180,  -399,   262,  -399,   264,
     262,  -399,    49,  -399,  -399,   181,  -399,  -399,   187,    49,
      49,   182,  -399,  -399,  -399,  -399,  -399,  -399,  -399,   183,
     185,   186,  -399,   189,  -399,   191,  -399,   193,  -399,   194,
    -399,   195,  -399,  -399,  -399,  -399,  -399,  -399,  -399,   273,
     275,  -399,   276,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
     196,  -399,  -399,  -399,  -399,   145,   278,  -399,   198,  -399,
     199,   200,    36,  -399,  -399,   102,  -399,   203,    -4,   204,
      -8,   280,  -399,   113,    49,  -399,  -399,   259,    86,    76,
    -399,   206,  -399,   207,  -399,   208,  -399,  -399,  -399,  -399,
    -399,  -399,  -399,   209,  -399,  -399,  -399,    49,  -399,   291,
     292,  -399,    49,  -399,  -399,  -399,    49,    82,    78,    39,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,   210,  -399,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,   290,  -399,  -399,
      12,  -399,  -399,  -399,  -399,    40,  -399,  -399,  -399,  -399,
     214,   217,   218,   222,  -399,   267,    -8,  -399,  -399,  -399,
    -399,  -399,  -399,    49,  -399,    49,   177,   236,   255,   224,
    -399,  -399,   211,   226,   228,   213,   219,   230,   233,   278,
    -399,    49,   113,  -399,   236,  -399,   255,    54,  -399,  -399,
    -399,  -399,   278,   231,  -399
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       0,     3,     4,     0,     6,     1,     9,     0,     5,     0,
     269,   270,     0,   234,     0,     0,     0,     0,   232,     2,
       0,     0,     0,     0,     0,     0,     0,   231,     0,     0,
       8,     0,    12,    13,    14,    15,    16,    17,    18,    19,
      21,    22,    23,    20,     0,    85,    86,   110,   111,    87,
      88,    89,    90,     7,     0,     0,     0,     0,     0,     0,
       0,    61,     0,    84,    60,     0,     0,     0,     0,     0,
      72,     0,     0,   229,   230,    31,     0,     0,     0,    10,
      11,   237,   235,     0,     0,     0,   114,   231,   112,   233,
     246,   244,   240,   242,   239,   259,   241,   231,    80,    81,
      82,    83,    50,   231,   231,   231,   231,   231,   231,    74,
      51,   222,   221,     0,     0,     0,     0,    56,   231,    79,
       0,    57,    59,   123,   124,   202,   203,   125,   218,   219,
       0,   231,     0,   268,    91,   238,   115,     0,   116,   120,
     121,   122,   216,   217,   220,     0,   249,   248,     0,   250,
       0,   243,     0,     0,     0,     0,    26,     0,    25,    24,
     256,   108,   106,   259,    93,     0,     0,     0,     0,     0,
       0,   253,     0,   253,     0,     0,   263,   259,   131,   132,
     133,   134,   136,   135,   137,   138,   139,   140,     0,   141,
     256,    98,     0,    96,    94,   259,     0,   103,    92,     0,
      77,    76,    78,    49,     0,     0,     0,   236,     0,   228,
     227,     0,   251,   252,   245,   265,     0,   231,   231,    47,
       0,     0,   231,   257,   258,   107,   109,     0,     0,     0,
     201,   172,   173,   171,     0,   154,   255,   254,   153,     0,
       0,     0,     0,   196,   192,     0,   191,   259,   184,   178,
     177,   176,     0,     0,     0,     0,    97,     0,    99,     0,
       0,    95,   231,   223,    65,     0,    63,    64,     0,   231,
     231,     0,   113,   247,   260,    28,    27,    75,    48,   261,
       0,     0,   214,     0,   215,     0,   175,     0,   163,     0,
     155,     0,   160,   161,   144,   145,   162,   142,   143,     0,
       0,   190,     0,   193,   186,   188,   187,   183,   185,   267,
       0,   159,   158,   165,   166,     0,     0,   105,     0,   102,
       0,     0,     0,    58,    73,    67,    46,     0,     0,     0,
     231,     0,    33,     0,   231,   209,   213,     0,     0,   253,
     200,     0,   198,     0,   199,     0,   264,   170,   169,   167,
     168,   164,   189,     0,   100,   101,   104,   231,   224,     0,
       0,    66,   231,    54,    53,    55,   231,     0,     0,     0,
     118,   126,   129,   127,   204,   205,   128,   266,     0,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    30,    29,   174,   149,   151,   148,     0,   146,   147,
       0,   195,   197,   194,   179,     0,    70,    68,    71,    69,
       0,     0,     0,     0,   130,   181,   231,   117,   262,   152,
     150,   156,   157,   231,   225,   231,     0,     0,     0,     0,
     180,   119,     0,     0,     0,     0,   207,     0,   211,     0,
     226,   231,     0,   206,     0,   210,     0,     0,    52,    32,
     208,   212,     0,     0,   182
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,   -83,
    -102,  -399,   -99,  -399,   -93,   188,  -399,  -399,  -350,  -399,
     -96,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,   141,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,   260,  -399,  -399,
    -399,    88,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
    -399,  -399,   -70,  -399,   -86,  -399,  -399,  -399,  -399,  -399,
    -399,  -399,  -399,  -399,  -399,  -399,  -321,   107,  -399,  -399,
    -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -399,
     -19,  -399,  -399,  -380,  -399,  -399,  -399,  -399,  -399,  -399,
     263,  -399,  -399,  -399,  -399,  -399,  -399,  -399,  -376,  -398,
     265,  -399,  -399,  -399,   -85,  -115,   -87,  -399,  -399,  -399,
    -399,   294,  -399,   266,  -399,  -399,  -399,  -168,   167,  -153,
    -399,  -399,  -399,  -399,  -399,  -399,    27
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     3,     4,     6,     8,     9,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,   280,
     391,    43,   153,   219,    75,    62,    71,   326,   327,   364,
     119,    63,   120,   265,   266,   267,   361,   407,   409,    72,
     325,   110,   278,   203,   102,    44,    45,   121,   198,   320,
     261,   318,   164,    46,    47,    48,   137,    88,   272,   369,
     138,   122,   370,   371,   123,   178,   297,   179,   398,   420,
     180,   238,   181,   421,   182,   312,   298,   289,   183,   315,
     351,   184,   233,   185,   287,   186,   251,   187,   414,   430,
     188,   307,   308,   353,   248,   301,   343,   345,   341,   189,
     124,   373,   374,   435,   125,   375,   437,   126,   283,   285,
     376,   127,   142,   128,   129,   144,    76,    49,    59,    50,
      54,    82,    51,    64,    96,   149,   214,   239,   225,   151,
     332,   253,   216,   378,   310,    52,    12
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     145,   139,   143,   199,   152,   241,   156,   111,   112,   159,
     226,   155,   410,   157,   158,   146,   154,   399,   154,   363,
     264,   154,   113,   113,   254,   249,   313,   147,     1,     2,
     438,   145,   190,   160,   205,    60,   114,   292,   206,   231,
     191,   161,   258,   293,   292,   314,   114,   115,   451,     7,
     293,   436,   294,   192,   367,   165,   193,   115,   220,   447,
       5,   304,   305,   194,   162,   368,   232,   166,   450,   250,
      61,   235,   453,   236,    53,   433,    55,   195,   163,   422,
     116,   116,    56,   296,   117,   237,    73,    74,   295,   118,
     296,   448,    10,    11,   303,    10,    11,    70,   148,   118,
     196,   197,   306,   236,    73,    74,   167,   118,   168,    70,
      57,   292,   394,   212,   169,   237,   412,   293,   109,   276,
     213,   170,   171,   172,   275,   173,   395,   174,   413,   281,
     223,   154,    58,    90,    91,   242,   175,   224,   243,   244,
      79,    92,   245,    81,   262,   357,   396,   322,   416,   423,
     246,   263,   358,   176,   177,   417,   424,   296,   397,    80,
     452,    73,    74,    93,    94,   404,   200,    83,   247,   201,
     202,   400,    86,   209,   210,   145,    87,   329,    95,    13,
      14,    15,   328,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,   379,   380,   381,
     382,   383,   384,   385,   386,   387,   388,   389,   390,    65,
      66,    67,    68,    69,   359,   360,    77,    78,    98,    99,
     100,   101,   347,   348,   349,   350,    84,    97,    85,   103,
     104,   105,   106,   107,   108,   130,   131,   132,   133,   136,
      60,   392,   405,   145,   372,   143,   150,   -62,   207,   204,
     211,   208,   215,   217,   230,   218,   221,   222,   255,   282,
     279,   227,   228,   229,   234,   240,   252,   257,   259,   260,
     145,   269,   270,   411,   271,   328,   273,   274,   284,   286,
     277,   288,   290,   299,   309,   317,   311,   319,   300,   302,
     316,   324,   323,   331,   333,   334,   340,   330,   342,   344,
     335,   352,   336,   377,   337,   338,   339,   346,   432,   354,
     355,   356,   362,   366,   406,   408,   393,   401,   402,   403,
     404,   418,   419,   425,   443,   444,   426,   440,   427,   145,
     372,   143,   428,   429,   439,   441,   145,   442,   328,   446,
     449,   445,   454,   434,   134,   268,   431,   291,   321,   415,
     140,   135,   141,    89,   328,   365,     0,   256
};

static const yytype_int16 yycheck[] =
{
      87,    87,    87,   118,    97,   173,   105,    23,    24,   108,
     163,   104,   362,   106,   107,    27,   103,   338,   105,    23,
      23,   108,    38,    38,   177,    26,    35,    39,     3,     4,
     428,   118,    29,    29,   130,    65,    62,    25,   131,    29,
      37,    37,   195,    31,    25,    54,    62,    73,   446,     8,
      31,   427,    33,    50,    62,    34,    53,    73,   154,   439,
       0,    41,    42,    60,    60,    73,    56,    46,   444,    70,
     100,    25,   452,    27,   108,   425,    99,    74,    74,   400,
      96,    96,    99,    71,   100,    39,   112,   113,    69,   115,
      71,   441,    99,   100,   247,    99,   100,   100,   110,   115,
      97,    98,    82,    27,   112,   113,    28,   115,    30,   100,
      99,    25,    26,    61,    36,    39,    34,    31,   104,   218,
      68,    43,    44,    45,   217,    47,    40,    49,    46,   222,
      61,   218,    99,    29,    30,    48,    58,    68,    51,    52,
     108,    37,    55,    99,   109,   109,    60,   262,   109,   109,
      63,   116,   116,    75,    76,   116,   116,    71,    72,   108,
     106,   112,   113,    59,    60,   111,   101,   114,    81,   104,
     105,   339,   110,    23,    24,   262,   114,   270,    74,     5,
       6,     7,   269,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    21,
      22,    23,    24,    25,   112,   113,    28,    29,   101,   102,
     103,   104,    77,    78,    79,    80,   114,   109,   114,   109,
     109,   109,   109,   109,   109,   109,   109,   109,   100,    23,
      65,   334,   357,   330,   330,   330,   110,   110,    99,   110,
      23,   111,    23,   109,    64,   109,   109,   109,    66,    23,
      83,   110,   110,   110,   110,   110,   110,   110,   110,   110,
     357,   109,   109,   366,   114,   362,   111,   111,    23,    23,
     104,    23,    67,   110,    23,    23,    29,    23,   110,   110,
     110,   104,   111,   110,   109,   109,    23,   115,    23,    23,
     111,    23,   111,    23,   111,   111,   111,   111,   423,   111,
     111,   111,   109,   109,    23,    23,    57,   111,   111,   111,
     111,   111,    32,   109,   111,   106,   109,   116,   110,   416,
     416,   416,   110,    66,   110,   109,   423,   109,   425,   106,
     442,   111,   111,   426,    84,   204,   416,   240,   260,   368,
      87,    85,    87,    59,   441,   328,    -1,   190
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     4,   118,   119,     0,   120,     8,   121,   122,
      99,   100,   253,     5,     6,     7,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   138,   162,   163,   170,   171,   172,   234,
     236,   239,   252,   108,   237,    99,    99,    99,    99,   235,
      65,   100,   142,   148,   240,   142,   142,   142,   142,   142,
     100,   143,   156,   112,   113,   141,   233,   142,   142,   108,
     108,    99,   238,   114,   114,   114,   110,   114,   174,   238,
      29,    30,    37,    59,    60,    74,   241,   109,   101,   102,
     103,   104,   161,   109,   109,   109,   109,   109,   109,   104,
     158,    23,    24,    38,    62,    73,    96,   100,   115,   147,
     149,   164,   178,   181,   217,   221,   224,   228,   230,   231,
     109,   109,   109,   100,   164,   240,    23,   173,   177,   181,
     217,   227,   229,   231,   232,   233,    27,    39,   110,   242,
     110,   246,   141,   139,   233,   141,   139,   141,   141,   139,
      29,    37,    60,    74,   169,    34,    46,    28,    30,    36,
      43,    44,    45,    47,    49,    58,    75,    76,   182,   184,
     187,   189,   191,   195,   198,   200,   202,   204,   207,   216,
      29,    37,    50,    53,    60,    74,    97,    98,   165,   232,
     101,   104,   105,   160,   110,   147,   141,    99,   111,    23,
      24,    23,    61,    68,   243,    23,   249,   109,   109,   140,
     147,   109,   109,    61,    68,   245,   246,   110,   110,   110,
      64,    29,    56,   199,   110,    25,    27,    39,   188,   244,
     110,   244,    48,    51,    52,    55,    63,    81,   211,    26,
      70,   203,   110,   248,   246,    66,   245,   110,   246,   110,
     110,   167,   109,   116,    23,   150,   151,   152,   156,   109,
     109,   114,   175,   111,   111,   141,   139,   104,   159,    83,
     136,   141,    23,   225,    23,   226,    23,   201,    23,   194,
      67,   194,    25,    31,    33,    69,    71,   183,   193,   110,
     110,   212,   110,   246,    41,    42,    82,   208,   209,    23,
     251,    29,   192,    35,    54,   196,   110,    23,   168,    23,
     166,   168,   232,   111,   104,   157,   144,   145,   233,   141,
     115,   110,   247,   109,   109,   111,   111,   111,   111,   111,
      23,   215,    23,   213,    23,   214,   111,    77,    78,    79,
      80,   197,    23,   210,   111,   111,   111,   109,   116,   112,
     113,   153,   109,    23,   146,   253,   109,    62,    73,   176,
     179,   180,   181,   218,   219,   222,   227,    23,   250,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,   137,   141,    57,    26,    40,    60,    72,   185,   193,
     244,   111,   111,   111,   111,   232,    23,   154,    23,   155,
     145,   141,    34,    46,   205,   207,   109,   116,   111,    32,
     186,   190,   193,   109,   116,   109,   109,   110,   110,    66,
     206,   179,   232,   145,   136,   220,   225,   223,   226,   110,
     116,   109,   109,   111,   106,   111,   106,   210,   145,   137,
     225,   226,   106,   210,   111
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   117,   118,   119,   119,   120,   120,   121,   122,   122,
     123,   123,   124,   124,   125,   125,   125,   125,   125,   125,
     125,   126,   126,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   137,   137,   137,   137,   137,
     137,   137,   137,   137,   137,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   146,   147,   147,   147,   147,
     148,   148,   149,   150,   150,   151,   152,   153,   153,   153,
     154,   155,   156,   157,   158,   159,   160,   160,   160,   160,
     161,   161,   161,   161,   161,   162,   162,   162,   162,   162,
     162,   163,   164,   164,   165,   165,   165,   165,   165,   165,
     165,   165,   166,   167,   167,   168,   169,   169,   169,   169,
     170,   170,   171,   172,   173,   173,   174,   175,   176,   176,
     177,   177,   177,   178,   178,   178,   179,   179,   179,   180,
     180,   181,   181,   181,   181,   181,   181,   181,   181,   181,
     181,   181,   182,   183,   183,   183,   184,   185,   185,   185,
     185,   185,   186,   187,   188,   188,   189,   190,   191,   192,
     193,   193,   193,   194,   195,   196,   196,   197,   197,   197,
     197,   198,   199,   199,   200,   201,   202,   203,   203,   204,
     205,   206,   206,   207,   208,   208,   209,   209,   209,   210,
     211,   211,   211,   211,   211,   211,   212,   212,   213,   214,
     215,   216,   217,   217,   218,   218,   219,   220,   220,   221,
     222,   223,   223,   224,   225,   226,   227,   227,   228,   228,
     229,   230,   230,   231,   231,   231,   231,   232,   232,   233,
     233,   233,   235,   234,   237,   236,   238,   238,   239,   240,
     240,   240,   240,   240,   240,   241,   242,   242,   242,   242,
     243,   243,   243,   244,   244,   244,   245,   245,   245,   246,
     246,   247,   247,   248,   248,   249,   250,   251,   252,   253,
     253
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     4,     1,     1,     2,     0,     3,     2,     0,
       2,     2,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     4,     4,     4,     6,     6,     8,
       8,     2,    12,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     6,     2,     2,     3,
       2,     2,     7,     2,     1,     1,     1,     1,     4,     1,
       1,     1,     1,     1,     1,     1,     3,     0,     2,     2,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     0,
       1,     1,     1,     1,     0,     1,     1,     1,     1,     1,
       1,     4,     2,     2,     1,     2,     1,     2,     1,     2,
       4,     4,     1,     0,     3,     1,     1,     2,     1,     2,
       1,     1,     3,     6,     0,     1,     2,     4,     1,     3,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     3,     1,     1,     1,     5,     1,     1,     1,
       2,     1,     1,     2,     1,     2,     6,     1,     3,     1,
       1,     1,     1,     1,     4,     1,     1,     1,     1,     1,
       1,     2,     1,     1,     5,     1,     2,     1,     1,     5,
       2,     0,     6,     3,     0,     1,     1,     1,     1,     1,
       2,     1,     1,     2,     4,     4,     0,     3,     1,     1,
       1,     2,     1,     1,     1,     1,     5,     1,     3,     5,
       5,     1,     3,     5,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     5,     7,     9,     2,     2,     1,
       1,     0,     0,     3,     0,     3,     3,     1,     4,     2,
       2,     2,     2,     3,     2,     3,     0,     3,     1,     1,
       0,     1,     1,     0,     1,     1,     0,     1,     1,     0,
       3,     0,     3,     0,     3,     1,     1,     1,     4,     1,
       1
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
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct asm_parser_state *state)
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
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct asm_parser_state *state)
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
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule, struct asm_parser_state *state)
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
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp, struct asm_parser_state *state)
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
yyparse (struct asm_parser_state *state)
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
        case 3:
#line 288 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->prog->Target != GL_VERTEX_PROGRAM_ARB) {
	      yyerror(& (yylsp[0]), state, "invalid fragment program header");

	   }
	   state->mode = ARB_vertex;
	}
#line 1972 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 4:
#line 296 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->prog->Target != GL_FRAGMENT_PROGRAM_ARB) {
	      yyerror(& (yylsp[0]), state, "invalid vertex program header");
	   }
	   state->mode = ARB_fragment;

	   state->option.TexRect =
	      (state->ctx->Extensions.NV_texture_rectangle != GL_FALSE);
	}
#line 1986 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 7:
#line 312 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   int valid = 0;

	   if (state->mode == ARB_vertex) {
	      valid = _mesa_ARBvp_parse_option(state, (yyvsp[-1].string));
	   } else if (state->mode == ARB_fragment) {
	      valid = _mesa_ARBfp_parse_option(state, (yyvsp[-1].string));
	   }


	   free((yyvsp[-1].string));

	   if (!valid) {
	      const char *const err_str = (state->mode == ARB_vertex)
		 ? "invalid ARB vertex program option"
		 : "invalid ARB fragment program option";

	      yyerror(& (yylsp[-1]), state, err_str);
	      YYERROR;
	   }
	}
#line 2012 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 10:
#line 340 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((yyvsp[-1].inst) != NULL) {
	      if (state->inst_tail == NULL) {
		 state->inst_head = (yyvsp[-1].inst);
	      } else {
		 state->inst_tail->next = (yyvsp[-1].inst);
	      }

	      state->inst_tail = (yyvsp[-1].inst);
	      (yyvsp[-1].inst)->next = NULL;

              state->prog->arb.NumInstructions++;
	   }
	}
#line 2031 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 12:
#line 358 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = (yyvsp[0].inst);
           state->prog->arb.NumAluInstructions++;
	}
#line 2040 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 13:
#line 363 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = (yyvsp[0].inst);
           state->prog->arb.NumTexInstructions++;
	}
#line 2049 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 24:
#line 384 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_ctor(OPCODE_ARL, & (yyvsp[-2].dst_reg), & (yyvsp[0].src_reg), NULL, NULL);
	}
#line 2057 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 25:
#line 390 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-3].temp_inst), & (yyvsp[-2].dst_reg), & (yyvsp[0].src_reg), NULL, NULL);
	}
#line 2065 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 26:
#line 396 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-3].temp_inst), & (yyvsp[-2].dst_reg), & (yyvsp[0].src_reg), NULL, NULL);
	}
#line 2073 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 27:
#line 402 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-5].temp_inst), & (yyvsp[-4].dst_reg), & (yyvsp[-2].src_reg), & (yyvsp[0].src_reg), NULL);
	}
#line 2081 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 28:
#line 409 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-5].temp_inst), & (yyvsp[-4].dst_reg), & (yyvsp[-2].src_reg), & (yyvsp[0].src_reg), NULL);
	}
#line 2089 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 29:
#line 416 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-7].temp_inst), & (yyvsp[-6].dst_reg), & (yyvsp[-4].src_reg), & (yyvsp[-2].src_reg), & (yyvsp[0].src_reg));
	}
#line 2097 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 30:
#line 422 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-7].temp_inst), & (yyvsp[-6].dst_reg), & (yyvsp[-4].src_reg), NULL, NULL);
	   if ((yyval.inst) != NULL) {
	      const GLbitfield tex_mask = (1U << (yyvsp[-2].integer));
	      GLbitfield shadow_tex = 0;
	      GLbitfield target_mask = 0;


	      (yyval.inst)->Base.TexSrcUnit = (yyvsp[-2].integer);

	      if ((yyvsp[0].integer) < 0) {
		 shadow_tex = tex_mask;

		 (yyval.inst)->Base.TexSrcTarget = -(yyvsp[0].integer);
		 (yyval.inst)->Base.TexShadow = 1;
	      } else {
		 (yyval.inst)->Base.TexSrcTarget = (yyvsp[0].integer);
	      }

	      target_mask = (1U << (yyval.inst)->Base.TexSrcTarget);

	      /* If this texture unit was previously accessed and that access
	       * had a different texture target, generate an error.
	       *
	       * If this texture unit was previously accessed and that access
	       * had a different shadow mode, generate an error.
	       */
	      if ((state->prog->TexturesUsed[(yyvsp[-2].integer)] != 0)
		  && ((state->prog->TexturesUsed[(yyvsp[-2].integer)] != target_mask)
		      || ((state->prog->ShadowSamplers & tex_mask)
			  != shadow_tex))) {
		 yyerror(& (yylsp[0]), state,
			 "multiple targets used on one texture image unit");
		 YYERROR;
	      }


	      state->prog->TexturesUsed[(yyvsp[-2].integer)] |= target_mask;
	      state->prog->ShadowSamplers |= shadow_tex;
	   }
	}
#line 2143 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 31:
#line 466 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_ctor(OPCODE_KIL, NULL, & (yyvsp[0].src_reg), NULL, NULL);
	   state->fragment.UsesKill = 1;
	}
#line 2152 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 32:
#line 473 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-11].temp_inst), & (yyvsp[-10].dst_reg), & (yyvsp[-8].src_reg), & (yyvsp[-6].src_reg), & (yyvsp[-4].src_reg));
	   if ((yyval.inst) != NULL) {
	      const GLbitfield tex_mask = (1U << (yyvsp[-2].integer));
	      GLbitfield shadow_tex = 0;
	      GLbitfield target_mask = 0;


	      (yyval.inst)->Base.TexSrcUnit = (yyvsp[-2].integer);

	      if ((yyvsp[0].integer) < 0) {
		 shadow_tex = tex_mask;

		 (yyval.inst)->Base.TexSrcTarget = -(yyvsp[0].integer);
		 (yyval.inst)->Base.TexShadow = 1;
	      } else {
		 (yyval.inst)->Base.TexSrcTarget = (yyvsp[0].integer);
	      }

	      target_mask = (1U << (yyval.inst)->Base.TexSrcTarget);

	      /* If this texture unit was previously accessed and that access
	       * had a different texture target, generate an error.
	       *
	       * If this texture unit was previously accessed and that access
	       * had a different shadow mode, generate an error.
	       */
	      if ((state->prog->TexturesUsed[(yyvsp[-2].integer)] != 0)
		  && ((state->prog->TexturesUsed[(yyvsp[-2].integer)] != target_mask)
		      || ((state->prog->ShadowSamplers & tex_mask)
			  != shadow_tex))) {
		 yyerror(& (yylsp[0]), state,
			 "multiple targets used on one texture image unit");
		 YYERROR;
	      }


	      state->prog->TexturesUsed[(yyvsp[-2].integer)] |= target_mask;
	      state->prog->ShadowSamplers |= shadow_tex;
	   }
	}
#line 2198 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 33:
#line 517 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 2206 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 34:
#line 522 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_1D_INDEX; }
#line 2212 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 35:
#line 523 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_2D_INDEX; }
#line 2218 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 36:
#line 524 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_3D_INDEX; }
#line 2224 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 37:
#line 525 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_CUBE_INDEX; }
#line 2230 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 38:
#line 526 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_RECT_INDEX; }
#line 2236 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 39:
#line 527 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = -TEXTURE_1D_INDEX; }
#line 2242 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 40:
#line 528 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = -TEXTURE_2D_INDEX; }
#line 2248 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 41:
#line 529 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = -TEXTURE_RECT_INDEX; }
#line 2254 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 42:
#line 530 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_1D_ARRAY_INDEX; }
#line 2260 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 43:
#line 531 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = TEXTURE_2D_ARRAY_INDEX; }
#line 2266 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 44:
#line 532 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = -TEXTURE_1D_ARRAY_INDEX; }
#line 2272 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 45:
#line 533 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = -TEXTURE_2D_ARRAY_INDEX; }
#line 2278 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 46:
#line 537 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   /* FIXME: Is this correct?  Should the extenedSwizzle be applied
	    * FIXME: to the existing swizzle?
	    */
	   (yyvsp[-2].src_reg).Base.Swizzle = (yyvsp[0].swiz_mask).swizzle;
	   (yyvsp[-2].src_reg).Base.Negate = (yyvsp[0].swiz_mask).mask;

	   (yyval.inst) = asm_instruction_copy_ctor(& (yyvsp[-5].temp_inst), & (yyvsp[-4].dst_reg), & (yyvsp[-2].src_reg), NULL, NULL);
	}
#line 2292 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 47:
#line 549 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.src_reg) = (yyvsp[0].src_reg);

	   if ((yyvsp[-1].negate)) {
	      (yyval.src_reg).Base.Negate = ~(yyval.src_reg).Base.Negate;
	   }
	}
#line 2304 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 48:
#line 559 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.src_reg) = (yyvsp[-1].src_reg);

	   (yyval.src_reg).Base.Swizzle = _mesa_combine_swizzles((yyval.src_reg).Base.Swizzle,
						    (yyvsp[0].swiz_mask).swizzle);
	}
#line 2315 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 49:
#line 568 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.src_reg) = (yyvsp[-1].src_reg);

	   if ((yyvsp[-2].negate)) {
	      (yyval.src_reg).Base.Negate = ~(yyval.src_reg).Base.Negate;
	   }

	   (yyval.src_reg).Base.Swizzle = _mesa_combine_swizzles((yyval.src_reg).Base.Swizzle,
						    (yyvsp[0].swiz_mask).swizzle);
	}
#line 2330 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 50:
#line 581 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.dst_reg) = (yyvsp[-1].dst_reg);
	   (yyval.dst_reg).WriteMask = (yyvsp[0].swiz_mask).mask;

	   if ((yyval.dst_reg).File == PROGRAM_OUTPUT) {
	      /* Technically speaking, this should check that it is in
	       * vertex program mode.  However, PositionInvariant can never be
	       * set in fragment program mode, so it is somewhat irrelevant.
	       */
	      if (state->option.PositionInvariant
	       && ((yyval.dst_reg).Index == VARYING_SLOT_POS)) {
		 yyerror(& (yylsp[-1]), state, "position-invariant programs cannot "
			 "write position");
		 YYERROR;
	      }

              state->prog->info.outputs_written |= BITFIELD64_BIT((yyval.dst_reg).Index);
	   }
	}
#line 2354 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 51:
#line 603 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   set_dst_reg(& (yyval.dst_reg), PROGRAM_ADDRESS, 0);
	   (yyval.dst_reg).WriteMask = (yyvsp[0].swiz_mask).mask;
	}
#line 2363 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 52:
#line 610 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   const unsigned xyzw_valid =
	      ((yyvsp[-6].ext_swizzle).xyzw_valid << 0)
	      | ((yyvsp[-4].ext_swizzle).xyzw_valid << 1)
	      | ((yyvsp[-2].ext_swizzle).xyzw_valid << 2)
	      | ((yyvsp[0].ext_swizzle).xyzw_valid << 3);
	   const unsigned rgba_valid =
	      ((yyvsp[-6].ext_swizzle).rgba_valid << 0)
	      | ((yyvsp[-4].ext_swizzle).rgba_valid << 1)
	      | ((yyvsp[-2].ext_swizzle).rgba_valid << 2)
	      | ((yyvsp[0].ext_swizzle).rgba_valid << 3);

	   /* All of the swizzle components have to be valid in either RGBA
	    * or XYZW.  Note that 0 and 1 are valid in both, so both masks
	    * can have some bits set.
	    *
	    * We somewhat deviate from the spec here.  It would be really hard
	    * to figure out which component is the error, and there probably
	    * isn't a lot of benefit.
	    */
	   if ((rgba_valid != 0x0f) && (xyzw_valid != 0x0f)) {
	      yyerror(& (yylsp[-6]), state, "cannot combine RGBA and XYZW swizzle "
		      "components");
	      YYERROR;
	   }

	   (yyval.swiz_mask).swizzle = MAKE_SWIZZLE4((yyvsp[-6].ext_swizzle).swz, (yyvsp[-4].ext_swizzle).swz, (yyvsp[-2].ext_swizzle).swz, (yyvsp[0].ext_swizzle).swz);
	   (yyval.swiz_mask).mask = ((yyvsp[-6].ext_swizzle).negate) | ((yyvsp[-4].ext_swizzle).negate << 1) | ((yyvsp[-2].ext_swizzle).negate << 2)
	      | ((yyvsp[0].ext_swizzle).negate << 3);
	}
#line 2398 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 53:
#line 643 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.ext_swizzle) = (yyvsp[0].ext_swizzle);
	   (yyval.ext_swizzle).negate = ((yyvsp[-1].negate)) ? 1 : 0;
	}
#line 2407 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 54:
#line 650 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (((yyvsp[0].integer) != 0) && ((yyvsp[0].integer) != 1)) {
	      yyerror(& (yylsp[0]), state, "invalid extended swizzle selector");
	      YYERROR;
	   }

	   (yyval.ext_swizzle).swz = ((yyvsp[0].integer) == 0) ? SWIZZLE_ZERO : SWIZZLE_ONE;
           (yyval.ext_swizzle).negate = 0;

	   /* 0 and 1 are valid for both RGBA swizzle names and XYZW
	    * swizzle names.
	    */
	   (yyval.ext_swizzle).xyzw_valid = 1;
	   (yyval.ext_swizzle).rgba_valid = 1;
	}
#line 2427 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 55:
#line 666 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   char s;

	   if (strlen((yyvsp[0].string)) > 1) {
	      yyerror(& (yylsp[0]), state, "invalid extended swizzle selector");
	      YYERROR;
	   }

	   s = (yyvsp[0].string)[0];
	   free((yyvsp[0].string));

           (yyval.ext_swizzle).rgba_valid = 0;
           (yyval.ext_swizzle).xyzw_valid = 0;
           (yyval.ext_swizzle).negate = 0;

	   switch (s) {
	   case 'x':
	      (yyval.ext_swizzle).swz = SWIZZLE_X;
	      (yyval.ext_swizzle).xyzw_valid = 1;
	      break;
	   case 'y':
	      (yyval.ext_swizzle).swz = SWIZZLE_Y;
	      (yyval.ext_swizzle).xyzw_valid = 1;
	      break;
	   case 'z':
	      (yyval.ext_swizzle).swz = SWIZZLE_Z;
	      (yyval.ext_swizzle).xyzw_valid = 1;
	      break;
	   case 'w':
	      (yyval.ext_swizzle).swz = SWIZZLE_W;
	      (yyval.ext_swizzle).xyzw_valid = 1;
	      break;

	   case 'r':
	      (yyval.ext_swizzle).swz = SWIZZLE_X;
	      (yyval.ext_swizzle).rgba_valid = 1;
	      break;
	   case 'g':
	      (yyval.ext_swizzle).swz = SWIZZLE_Y;
	      (yyval.ext_swizzle).rgba_valid = 1;
	      break;
	   case 'b':
	      (yyval.ext_swizzle).swz = SWIZZLE_Z;
	      (yyval.ext_swizzle).rgba_valid = 1;
	      break;
	   case 'a':
	      (yyval.ext_swizzle).swz = SWIZZLE_W;
	      (yyval.ext_swizzle).rgba_valid = 1;
	      break;

	   default:
	      yyerror(& (yylsp[0]), state, "invalid extended swizzle selector");
	      YYERROR;
	      break;
	   }
	}
#line 2488 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 56:
#line 725 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s = (struct asm_symbol *)
              _mesa_symbol_table_find_symbol(state->st, (yyvsp[0].string));

	   free((yyvsp[0].string));

	   if (s == NULL) {
	      yyerror(& (yylsp[0]), state, "invalid operand variable");
	      YYERROR;
	   } else if ((s->type != at_param) && (s->type != at_temp)
		      && (s->type != at_attrib)) {
	      yyerror(& (yylsp[0]), state, "invalid operand variable");
	      YYERROR;
	   } else if ((s->type == at_param) && s->param_is_array) {
	      yyerror(& (yylsp[0]), state, "non-array access to array PARAM");
	      YYERROR;
	   }

	   init_src_reg(& (yyval.src_reg));
	   switch (s->type) {
	   case at_temp:
	      set_src_reg(& (yyval.src_reg), PROGRAM_TEMPORARY, s->temp_binding);
	      break;
	   case at_param:
              set_src_reg_swz(& (yyval.src_reg), s->param_binding_type,
                              s->param_binding_begin,
                              s->param_binding_swizzle);
	      break;
	   case at_attrib:
	      set_src_reg(& (yyval.src_reg), PROGRAM_INPUT, s->attrib_binding);
              state->prog->info.inputs_read |= BITFIELD64_BIT((yyval.src_reg).Base.Index);

	      if (!validate_inputs(& (yylsp[0]), state)) {
		 YYERROR;
	      }
	      break;

	   default:
	      YYERROR;
	      break;
	   }
	}
#line 2535 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 57:
#line 768 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   set_src_reg(& (yyval.src_reg), PROGRAM_INPUT, (yyvsp[0].attrib));
           state->prog->info.inputs_read |= BITFIELD64_BIT((yyval.src_reg).Base.Index);

	   if (!validate_inputs(& (yylsp[0]), state)) {
	      YYERROR;
	   }
	}
#line 2548 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 58:
#line 777 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (! (yyvsp[-1].src_reg).Base.RelAddr
	       && ((unsigned) (yyvsp[-1].src_reg).Base.Index >= (yyvsp[-3].sym)->param_binding_length)) {
	      yyerror(& (yylsp[-1]), state, "out of bounds array access");
	      YYERROR;
	   }

	   init_src_reg(& (yyval.src_reg));
	   (yyval.src_reg).Base.File = (yyvsp[-3].sym)->param_binding_type;

	   if ((yyvsp[-1].src_reg).Base.RelAddr) {
              state->prog->arb.IndirectRegisterFiles |= (1 << (yyval.src_reg).Base.File);
	      (yyvsp[-3].sym)->param_accessed_indirectly = 1;

	      (yyval.src_reg).Base.RelAddr = 1;
	      (yyval.src_reg).Base.Index = (yyvsp[-1].src_reg).Base.Index;
	      (yyval.src_reg).Symbol = (yyvsp[-3].sym);
	   } else {
	      (yyval.src_reg).Base.Index = (yyvsp[-3].sym)->param_binding_begin + (yyvsp[-1].src_reg).Base.Index;
	   }
	}
#line 2574 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 59:
#line 799 "./program/program_parse.y" /* yacc.c:1646  */
    {
           gl_register_file file = ((yyvsp[0].temp_sym).name != NULL) 
	      ? (yyvsp[0].temp_sym).param_binding_type
	      : PROGRAM_CONSTANT;
           set_src_reg_swz(& (yyval.src_reg), file, (yyvsp[0].temp_sym).param_binding_begin,
                           (yyvsp[0].temp_sym).param_binding_swizzle);
	}
#line 2586 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 60:
#line 809 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   set_dst_reg(& (yyval.dst_reg), PROGRAM_OUTPUT, (yyvsp[0].result));
	}
#line 2594 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 61:
#line 813 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s = (struct asm_symbol *)
              _mesa_symbol_table_find_symbol(state->st, (yyvsp[0].string));

	   free((yyvsp[0].string));

	   if (s == NULL) {
	      yyerror(& (yylsp[0]), state, "invalid operand variable");
	      YYERROR;
	   } else if ((s->type != at_output) && (s->type != at_temp)) {
	      yyerror(& (yylsp[0]), state, "invalid operand variable");
	      YYERROR;
	   }

	   switch (s->type) {
	   case at_temp:
	      set_dst_reg(& (yyval.dst_reg), PROGRAM_TEMPORARY, s->temp_binding);
	      break;
	   case at_output:
	      set_dst_reg(& (yyval.dst_reg), PROGRAM_OUTPUT, s->output_binding);
	      break;
	   default:
	      set_dst_reg(& (yyval.dst_reg), s->param_binding_type, s->param_binding_begin);
	      break;
	   }
	}
#line 2625 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 62:
#line 842 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s = (struct asm_symbol *)
              _mesa_symbol_table_find_symbol(state->st, (yyvsp[0].string));

	   free((yyvsp[0].string));

	   if (s == NULL) {
	      yyerror(& (yylsp[0]), state, "invalid operand variable");
	      YYERROR;
	   } else if ((s->type != at_param) || !s->param_is_array) {
	      yyerror(& (yylsp[0]), state, "array access to non-PARAM variable");
	      YYERROR;
	   } else {
	      (yyval.sym) = s;
	   }
	}
#line 2646 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 65:
#line 863 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   init_src_reg(& (yyval.src_reg));
	   (yyval.src_reg).Base.Index = (yyvsp[0].integer);
	}
#line 2655 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 66:
#line 870 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   /* FINISHME: Add support for multiple address registers.
	    */
	   /* FINISHME: Add support for 4-component address registers.
	    */
	   init_src_reg(& (yyval.src_reg));
	   (yyval.src_reg).Base.RelAddr = 1;
	   (yyval.src_reg).Base.Index = (yyvsp[0].integer);
	}
#line 2669 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 67:
#line 881 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 2675 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 68:
#line 882 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = (yyvsp[0].integer); }
#line 2681 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 69:
#line 883 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = -(yyvsp[0].integer); }
#line 2687 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 70:
#line 887 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (((yyvsp[0].integer) < 0) || ((yyvsp[0].integer) > (state->limits->MaxAddressOffset - 1))) {
              char s[100];
              _mesa_snprintf(s, sizeof(s),
                             "relative address offset too large (%d)", (yyvsp[0].integer));
	      yyerror(& (yylsp[0]), state, s);
	      YYERROR;
	   } else {
	      (yyval.integer) = (yyvsp[0].integer);
	   }
	}
#line 2703 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 71:
#line 901 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (((yyvsp[0].integer) < 0) || ((yyvsp[0].integer) > state->limits->MaxAddressOffset)) {
              char s[100];
              _mesa_snprintf(s, sizeof(s),
                             "relative address offset too large (%d)", (yyvsp[0].integer));
	      yyerror(& (yylsp[0]), state, s);
	      YYERROR;
	   } else {
	      (yyval.integer) = (yyvsp[0].integer);
	   }
	}
#line 2719 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 72:
#line 915 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s = (struct asm_symbol *)
              _mesa_symbol_table_find_symbol(state->st, (yyvsp[0].string));

	   free((yyvsp[0].string));

	   if (s == NULL) {
	      yyerror(& (yylsp[0]), state, "invalid array member");
	      YYERROR;
	   } else if (s->type != at_address) {
	      yyerror(& (yylsp[0]), state,
		      "invalid variable for indexed array access");
	      YYERROR;
	   } else {
	      (yyval.sym) = s;
	   }
	}
#line 2741 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 73:
#line 935 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((yyvsp[0].swiz_mask).mask != WRITEMASK_X) {
	      yyerror(& (yylsp[0]), state, "invalid address component selector");
	      YYERROR;
	   } else {
	      (yyval.swiz_mask) = (yyvsp[0].swiz_mask);
	   }
	}
#line 2754 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 74:
#line 946 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((yyvsp[0].swiz_mask).mask != WRITEMASK_X) {
	      yyerror(& (yylsp[0]), state,
		      "address register write mask must be \".x\"");
	      YYERROR;
	   } else {
	      (yyval.swiz_mask) = (yyvsp[0].swiz_mask);
	   }
	}
#line 2768 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 79:
#line 962 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.swiz_mask).swizzle = SWIZZLE_NOOP; (yyval.swiz_mask).mask = WRITEMASK_XYZW; }
#line 2774 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 84:
#line 966 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.swiz_mask).swizzle = SWIZZLE_NOOP; (yyval.swiz_mask).mask = WRITEMASK_XYZW; }
#line 2780 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 91:
#line 978 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s =
	      declare_variable(state, (yyvsp[-2].string), at_attrib, & (yylsp[-2]));

	   if (s == NULL) {
	      free((yyvsp[-2].string));
	      YYERROR;
	   } else {
	      s->attrib_binding = (yyvsp[0].attrib);
	      state->InputsBound |= BITFIELD64_BIT(s->attrib_binding);

	      if (!validate_inputs(& (yylsp[0]), state)) {
		 YYERROR;
	      }
	   }
	}
#line 2801 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 92:
#line 997 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = (yyvsp[0].attrib);
	}
#line 2809 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 93:
#line 1001 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = (yyvsp[0].attrib);
	}
#line 2817 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 94:
#line 1007 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_POS;
	}
#line 2825 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 95:
#line 1011 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_WEIGHT;
	}
#line 2833 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 96:
#line 1015 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_NORMAL;
	}
#line 2841 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 97:
#line 1019 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_COLOR0 + (yyvsp[0].integer);
	}
#line 2849 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 98:
#line 1023 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_FOG;
	}
#line 2857 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 99:
#line 1027 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_TEX0 + (yyvsp[0].integer);
	}
#line 2865 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 100:
#line 1031 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   yyerror(& (yylsp[-3]), state, "GL_ARB_matrix_palette not supported");
	   YYERROR;
	}
#line 2874 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 101:
#line 1036 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VERT_ATTRIB_GENERIC0 + (yyvsp[-1].integer);
	}
#line 2882 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 102:
#line 1042 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->limits->MaxAttribs) {
	      yyerror(& (yylsp[0]), state, "invalid vertex attribute reference");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 2895 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 106:
#line 1056 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VARYING_SLOT_POS;
	}
#line 2903 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 107:
#line 1060 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VARYING_SLOT_COL0 + (yyvsp[0].integer);
	}
#line 2911 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 108:
#line 1064 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VARYING_SLOT_FOGC;
	}
#line 2919 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 109:
#line 1068 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.attrib) = VARYING_SLOT_TEX0 + (yyvsp[0].integer);
	}
#line 2927 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 112:
#line 1076 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s =
	      declare_variable(state, (yyvsp[-1].string), at_param, & (yylsp[-1]));

	   if (s == NULL) {
	      free((yyvsp[-1].string));
	      YYERROR;
	   } else {
	      s->param_binding_type = (yyvsp[0].temp_sym).param_binding_type;
	      s->param_binding_begin = (yyvsp[0].temp_sym).param_binding_begin;
	      s->param_binding_length = (yyvsp[0].temp_sym).param_binding_length;
              s->param_binding_swizzle = (yyvsp[0].temp_sym).param_binding_swizzle;
	      s->param_is_array = 0;
	   }
	}
#line 2947 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 113:
#line 1094 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (((yyvsp[-2].integer) != 0) && ((unsigned) (yyvsp[-2].integer) != (yyvsp[0].temp_sym).param_binding_length)) {
	      free((yyvsp[-4].string));
	      yyerror(& (yylsp[-2]), state, 
		      "parameter array size and number of bindings must match");
	      YYERROR;
	   } else {
	      struct asm_symbol *const s =
		 declare_variable(state, (yyvsp[-4].string), (yyvsp[0].temp_sym).type, & (yylsp[-4]));

	      if (s == NULL) {
		 free((yyvsp[-4].string));
		 YYERROR;
	      } else {
		 s->param_binding_type = (yyvsp[0].temp_sym).param_binding_type;
		 s->param_binding_begin = (yyvsp[0].temp_sym).param_binding_begin;
		 s->param_binding_length = (yyvsp[0].temp_sym).param_binding_length;
                 s->param_binding_swizzle = SWIZZLE_XYZW;
		 s->param_is_array = 1;
	      }
	   }
	}
#line 2974 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 114:
#line 1119 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = 0;
	}
#line 2982 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 115:
#line 1123 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (((yyvsp[0].integer) < 1) || ((unsigned) (yyvsp[0].integer) > state->limits->MaxParameters)) {
              char msg[100];
              _mesa_snprintf(msg, sizeof(msg),
                             "invalid parameter array size (size=%d max=%u)",
                             (yyvsp[0].integer), state->limits->MaxParameters);
	      yyerror(& (yylsp[0]), state, msg);
	      YYERROR;
	   } else {
	      (yyval.integer) = (yyvsp[0].integer);
	   }
	}
#line 2999 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 116:
#line 1138 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.temp_sym) = (yyvsp[0].temp_sym);
	}
#line 3007 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 117:
#line 1144 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.temp_sym) = (yyvsp[-1].temp_sym);
	}
#line 3015 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 119:
#line 1151 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyvsp[-2].temp_sym).param_binding_length += (yyvsp[0].temp_sym).param_binding_length;
	   (yyval.temp_sym) = (yyvsp[-2].temp_sym);
	}
#line 3024 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 120:
#line 1158 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_state(state->prog, & (yyval.temp_sym), (yyvsp[0].state));
	}
#line 3034 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 121:
#line 1164 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_param(state->prog, & (yyval.temp_sym), (yyvsp[0].state));
	}
#line 3044 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 122:
#line 1170 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_const(state->prog, & (yyval.temp_sym), & (yyvsp[0].vector), GL_TRUE);
	}
#line 3054 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 123:
#line 1178 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_state(state->prog, & (yyval.temp_sym), (yyvsp[0].state));
	}
#line 3064 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 124:
#line 1184 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_param(state->prog, & (yyval.temp_sym), (yyvsp[0].state));
	}
#line 3074 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 125:
#line 1190 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_const(state->prog, & (yyval.temp_sym), & (yyvsp[0].vector), GL_TRUE);
	}
#line 3084 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 126:
#line 1198 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_state(state->prog, & (yyval.temp_sym), (yyvsp[0].state));
	}
#line 3094 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 127:
#line 1204 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_param(state->prog, & (yyval.temp_sym), (yyvsp[0].state));
	}
#line 3104 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 128:
#line 1210 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset(& (yyval.temp_sym), 0, sizeof((yyval.temp_sym)));
	   (yyval.temp_sym).param_binding_begin = ~0;
	   initialize_symbol_from_const(state->prog, & (yyval.temp_sym), & (yyvsp[0].vector), GL_FALSE);
	}
#line 3114 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 129:
#line 1217 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3120 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 130:
#line 1218 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3126 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 131:
#line 1221 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3132 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 132:
#line 1222 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3138 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 133:
#line 1223 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3144 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 134:
#line 1224 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3150 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 135:
#line 1225 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3156 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 136:
#line 1226 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3162 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 137:
#line 1227 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3168 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 138:
#line 1228 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3174 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 139:
#line 1229 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3180 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 140:
#line 1230 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3186 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 141:
#line 1231 "./program/program_parse.y" /* yacc.c:1646  */
    { memcpy((yyval.state), (yyvsp[0].state), sizeof((yyval.state))); }
#line 3192 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 142:
#line 1235 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_MATERIAL;
	   (yyval.state)[1] = (yyvsp[-1].integer);
	   (yyval.state)[2] = (yyvsp[0].integer);
	}
#line 3203 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 143:
#line 1244 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3211 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 144:
#line 1248 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_EMISSION;
	}
#line 3219 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 145:
#line 1252 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_SHININESS;
	}
#line 3227 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 146:
#line 1258 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_LIGHT;
	   (yyval.state)[1] = (yyvsp[-2].integer);
	   (yyval.state)[2] = (yyvsp[0].integer);
	}
#line 3238 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 147:
#line 1267 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3246 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 148:
#line 1271 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_POSITION;
	}
#line 3254 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 149:
#line 1275 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (!state->ctx->Extensions.EXT_point_parameters) {
	      yyerror(& (yylsp[0]), state, "GL_ARB_point_parameters not supported");
	      YYERROR;
	   }

	   (yyval.integer) = STATE_ATTENUATION;
	}
#line 3267 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 150:
#line 1284 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3275 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 151:
#line 1288 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_HALF_VECTOR;
	}
#line 3283 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 152:
#line 1294 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_SPOT_DIRECTION;
	}
#line 3291 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 153:
#line 1300 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[0].state)[0];
	   (yyval.state)[1] = (yyvsp[0].state)[1];
	}
#line 3300 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 154:
#line 1307 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_LIGHTMODEL_AMBIENT;
	}
#line 3309 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 155:
#line 1312 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_LIGHTMODEL_SCENECOLOR;
	   (yyval.state)[1] = (yyvsp[-1].integer);
	}
#line 3319 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 156:
#line 1320 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_LIGHTPROD;
	   (yyval.state)[1] = (yyvsp[-3].integer);
	   (yyval.state)[2] = (yyvsp[-1].integer);
	   (yyval.state)[3] = (yyvsp[0].integer);
	}
#line 3331 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 158:
#line 1332 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = (yyvsp[0].integer);
	   (yyval.state)[1] = (yyvsp[-1].integer);
	}
#line 3341 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 159:
#line 1340 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXENV_COLOR;
	}
#line 3349 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 160:
#line 1346 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_AMBIENT;
	}
#line 3357 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 161:
#line 1350 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_DIFFUSE;
	}
#line 3365 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 162:
#line 1354 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_SPECULAR;
	}
#line 3373 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 163:
#line 1360 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->MaxLights) {
	      yyerror(& (yylsp[0]), state, "invalid light selector");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3386 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 164:
#line 1371 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_TEXGEN;
	   (yyval.state)[1] = (yyvsp[-2].integer);
	   (yyval.state)[2] = (yyvsp[-1].integer) + (yyvsp[0].integer);
	}
#line 3397 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 165:
#line 1380 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXGEN_EYE_S;
	}
#line 3405 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 166:
#line 1384 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXGEN_OBJECT_S;
	}
#line 3413 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 167:
#line 1389 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXGEN_EYE_S - STATE_TEXGEN_EYE_S;
	}
#line 3421 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 168:
#line 1393 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXGEN_EYE_T - STATE_TEXGEN_EYE_S;
	}
#line 3429 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 169:
#line 1397 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXGEN_EYE_R - STATE_TEXGEN_EYE_S;
	}
#line 3437 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 170:
#line 1401 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_TEXGEN_EYE_Q - STATE_TEXGEN_EYE_S;
	}
#line 3445 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 171:
#line 1407 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = (yyvsp[0].integer);
	}
#line 3454 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 172:
#line 1414 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_FOG_COLOR;
	}
#line 3462 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 173:
#line 1418 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_FOG_PARAMS;
	}
#line 3470 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 174:
#line 1424 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_CLIPPLANE;
	   (yyval.state)[1] = (yyvsp[-2].integer);
	}
#line 3480 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 175:
#line 1432 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->MaxClipPlanes) {
	      yyerror(& (yylsp[0]), state, "invalid clip plane selector");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3493 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 176:
#line 1443 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = (yyvsp[0].integer);
	}
#line 3502 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 177:
#line 1450 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_POINT_SIZE;
	}
#line 3510 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 178:
#line 1454 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_POINT_ATTENUATION;
	}
#line 3518 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 179:
#line 1460 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[-4].state)[0];
	   (yyval.state)[1] = (yyvsp[-4].state)[1];
	   (yyval.state)[2] = (yyvsp[-1].integer);
	   (yyval.state)[3] = (yyvsp[-1].integer);
	   (yyval.state)[4] = (yyvsp[-4].state)[2];
	}
#line 3530 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 180:
#line 1470 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[-1].state)[0];
	   (yyval.state)[1] = (yyvsp[-1].state)[1];
	   (yyval.state)[2] = (yyvsp[0].state)[2];
	   (yyval.state)[3] = (yyvsp[0].state)[3];
	   (yyval.state)[4] = (yyvsp[-1].state)[2];
	}
#line 3542 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 181:
#line 1480 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[2] = 0;
	   (yyval.state)[3] = 3;
	}
#line 3551 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 182:
#line 1485 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   /* It seems logical that the matrix row range specifier would have
	    * to specify a range or more than one row (i.e., $5 > $3).
	    * However, the ARB_vertex_program spec says "a program will fail
	    * to load if <a> is greater than <b>."  This means that $3 == $5
	    * is valid.
	    */
	   if ((yyvsp[-3].integer) > (yyvsp[-1].integer)) {
	      yyerror(& (yylsp[-3]), state, "invalid matrix row range");
	      YYERROR;
	   }

	   (yyval.state)[2] = (yyvsp[-3].integer);
	   (yyval.state)[3] = (yyvsp[-1].integer);
	}
#line 3571 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 183:
#line 1503 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[-1].state)[0];
	   (yyval.state)[1] = (yyvsp[-1].state)[1];
	   (yyval.state)[2] = (yyvsp[0].integer);
	}
#line 3581 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 184:
#line 1511 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = 0;
	}
#line 3589 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 185:
#line 1515 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3597 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 186:
#line 1521 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_MATRIX_INVERSE;
	}
#line 3605 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 187:
#line 1525 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_MATRIX_TRANSPOSE;
	}
#line 3613 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 188:
#line 1529 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = STATE_MATRIX_INVTRANS;
	}
#line 3621 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 189:
#line 1535 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((yyvsp[0].integer) > 3) {
	      yyerror(& (yylsp[0]), state, "invalid matrix row reference");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3634 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 190:
#line 1546 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = STATE_MODELVIEW_MATRIX;
	   (yyval.state)[1] = (yyvsp[0].integer);
	}
#line 3643 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 191:
#line 1551 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = STATE_PROJECTION_MATRIX;
	   (yyval.state)[1] = 0;
	}
#line 3652 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 192:
#line 1556 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = STATE_MVP_MATRIX;
	   (yyval.state)[1] = 0;
	}
#line 3661 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 193:
#line 1561 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = STATE_TEXTURE_MATRIX;
	   (yyval.state)[1] = (yyvsp[0].integer);
	}
#line 3670 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 194:
#line 1566 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   yyerror(& (yylsp[-3]), state, "GL_ARB_matrix_palette not supported");
	   YYERROR;
	}
#line 3679 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 195:
#line 1571 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = STATE_PROGRAM_MATRIX;
	   (yyval.state)[1] = (yyvsp[-1].integer);
	}
#line 3688 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 196:
#line 1578 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = 0;
	}
#line 3696 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 197:
#line 1582 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = (yyvsp[-1].integer);
	}
#line 3704 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 198:
#line 1587 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   /* Since GL_ARB_vertex_blend isn't supported, only modelview matrix
	    * zero is valid.
	    */
	   if ((yyvsp[0].integer) != 0) {
	      yyerror(& (yylsp[0]), state, "invalid modelview matrix index");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3720 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 199:
#line 1600 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   /* Since GL_ARB_matrix_palette isn't supported, just let any value
	    * through here.  The error will be generated later.
	    */
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3731 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 200:
#line 1608 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->MaxProgramMatrices) {
	      yyerror(& (yylsp[0]), state, "invalid program matrix selector");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3744 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 201:
#line 1619 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = STATE_DEPTH_RANGE;
	}
#line 3753 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 206:
#line 1631 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = state->state_param_enum;
	   (yyval.state)[1] = STATE_ENV;
	   (yyval.state)[2] = (yyvsp[-1].state)[0];
	   (yyval.state)[3] = (yyvsp[-1].state)[1];
	}
#line 3765 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 207:
#line 1641 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[0].integer);
	   (yyval.state)[1] = (yyvsp[0].integer);
	}
#line 3774 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 208:
#line 1646 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[-2].integer);
	   (yyval.state)[1] = (yyvsp[0].integer);
	}
#line 3783 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 209:
#line 1653 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = state->state_param_enum;
	   (yyval.state)[1] = STATE_ENV;
	   (yyval.state)[2] = (yyvsp[-1].integer);
	   (yyval.state)[3] = (yyvsp[-1].integer);
	}
#line 3795 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 210:
#line 1663 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = state->state_param_enum;
	   (yyval.state)[1] = STATE_LOCAL;
	   (yyval.state)[2] = (yyvsp[-1].state)[0];
	   (yyval.state)[3] = (yyvsp[-1].state)[1];
	}
#line 3807 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 211:
#line 1672 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[0].integer);
	   (yyval.state)[1] = (yyvsp[0].integer);
	}
#line 3816 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 212:
#line 1677 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.state)[0] = (yyvsp[-2].integer);
	   (yyval.state)[1] = (yyvsp[0].integer);
	}
#line 3825 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 213:
#line 1684 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   memset((yyval.state), 0, sizeof((yyval.state)));
	   (yyval.state)[0] = state->state_param_enum;
	   (yyval.state)[1] = STATE_LOCAL;
	   (yyval.state)[2] = (yyvsp[-1].integer);
	   (yyval.state)[3] = (yyvsp[-1].integer);
	}
#line 3837 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 214:
#line 1694 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->limits->MaxEnvParams) {
	      yyerror(& (yylsp[0]), state, "invalid environment parameter reference");
	      YYERROR;
	   }
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3849 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 215:
#line 1704 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->limits->MaxLocalParams) {
	      yyerror(& (yylsp[0]), state, "invalid local parameter reference");
	      YYERROR;
	   }
	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 3861 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 220:
#line 1719 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 4;
	   (yyval.vector).data[0].f = (yyvsp[0].real);
	   (yyval.vector).data[1].f = (yyvsp[0].real);
	   (yyval.vector).data[2].f = (yyvsp[0].real);
	   (yyval.vector).data[3].f = (yyvsp[0].real);
	}
#line 3873 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 221:
#line 1729 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 1;
	   (yyval.vector).data[0].f = (yyvsp[0].real);
	   (yyval.vector).data[1].f = (yyvsp[0].real);
	   (yyval.vector).data[2].f = (yyvsp[0].real);
	   (yyval.vector).data[3].f = (yyvsp[0].real);
	}
#line 3885 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 222:
#line 1737 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 1;
	   (yyval.vector).data[0].f = (float) (yyvsp[0].integer);
	   (yyval.vector).data[1].f = (float) (yyvsp[0].integer);
	   (yyval.vector).data[2].f = (float) (yyvsp[0].integer);
	   (yyval.vector).data[3].f = (float) (yyvsp[0].integer);
	}
#line 3897 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 223:
#line 1747 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 4;
	   (yyval.vector).data[0].f = (yyvsp[-1].real);
	   (yyval.vector).data[1].f = 0.0f;
	   (yyval.vector).data[2].f = 0.0f;
	   (yyval.vector).data[3].f = 1.0f;
	}
#line 3909 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 224:
#line 1755 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 4;
	   (yyval.vector).data[0].f = (yyvsp[-3].real);
	   (yyval.vector).data[1].f = (yyvsp[-1].real);
	   (yyval.vector).data[2].f = 0.0f;
	   (yyval.vector).data[3].f = 1.0f;
	}
#line 3921 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 225:
#line 1764 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 4;
	   (yyval.vector).data[0].f = (yyvsp[-5].real);
	   (yyval.vector).data[1].f = (yyvsp[-3].real);
	   (yyval.vector).data[2].f = (yyvsp[-1].real);
	   (yyval.vector).data[3].f = 1.0f;
	}
#line 3933 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 226:
#line 1773 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.vector).count = 4;
	   (yyval.vector).data[0].f = (yyvsp[-7].real);
	   (yyval.vector).data[1].f = (yyvsp[-5].real);
	   (yyval.vector).data[2].f = (yyvsp[-3].real);
	   (yyval.vector).data[3].f = (yyvsp[-1].real);
	}
#line 3945 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 227:
#line 1783 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.real) = ((yyvsp[-1].negate)) ? -(yyvsp[0].real) : (yyvsp[0].real);
	}
#line 3953 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 228:
#line 1787 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.real) = (float)(((yyvsp[-1].negate)) ? -(yyvsp[0].integer) : (yyvsp[0].integer));
	}
#line 3961 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 229:
#line 1792 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.negate) = FALSE; }
#line 3967 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 230:
#line 1793 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.negate) = TRUE;  }
#line 3973 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 231:
#line 1794 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.negate) = FALSE; }
#line 3979 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 232:
#line 1797 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = (yyvsp[0].integer); }
#line 3985 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 234:
#line 1800 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = (yyvsp[0].integer); }
#line 3991 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 236:
#line 1804 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (!declare_variable(state, (yyvsp[0].string), (yyvsp[-3].integer), & (yylsp[0]))) {
	      free((yyvsp[0].string));
	      YYERROR;
	   }
	}
#line 4002 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 237:
#line 1811 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (!declare_variable(state, (yyvsp[0].string), (yyvsp[-1].integer), & (yylsp[0]))) {
	      free((yyvsp[0].string));
	      YYERROR;
	   }
	}
#line 4013 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 238:
#line 1820 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *const s =
	      declare_variable(state, (yyvsp[-2].string), at_output, & (yylsp[-2]));

	   if (s == NULL) {
	      free((yyvsp[-2].string));
	      YYERROR;
	   } else {
	      s->output_binding = (yyvsp[0].result);
	   }
	}
#line 4029 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 239:
#line 1834 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.result) = VARYING_SLOT_POS;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4042 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 240:
#line 1843 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.result) = VARYING_SLOT_FOGC;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4055 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 241:
#line 1852 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.result) = (yyvsp[0].result);
	}
#line 4063 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 242:
#line 1856 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.result) = VARYING_SLOT_PSIZ;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4076 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 243:
#line 1865 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.result) = VARYING_SLOT_TEX0 + (yyvsp[0].integer);
	   } else {
	      yyerror(& (yylsp[-1]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4089 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 244:
#line 1874 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_fragment) {
	      (yyval.result) = FRAG_RESULT_DEPTH;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4102 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 245:
#line 1885 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.result) = (yyvsp[-1].integer) + (yyvsp[0].integer);
	}
#line 4110 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 246:
#line 1891 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.integer) = VARYING_SLOT_COL0;
	   } else {
	      if (state->option.DrawBuffers)
		 (yyval.integer) = FRAG_RESULT_DATA0;
	      else
		 (yyval.integer) = FRAG_RESULT_COLOR;
	   }
	}
#line 4125 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 247:
#line 1902 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      yyerror(& (yylsp[-2]), state, "invalid program result name");
	      YYERROR;
	   } else {
	      if (!state->option.DrawBuffers) {
		 /* From the ARB_draw_buffers spec (same text exists
		  * for ATI_draw_buffers):
		  *
		  *     If this option is not specified, a fragment
		  *     program that attempts to bind
		  *     "result.color[n]" will fail to load, and only
		  *     "result.color" will be allowed.
		  */
		 yyerror(& (yylsp[-2]), state,
			 "result.color[] used without "
			 "`OPTION ARB_draw_buffers' or "
			 "`OPTION ATI_draw_buffers'");
		 YYERROR;
	      } else if ((yyvsp[-1].integer) >= state->MaxDrawBuffers) {
		 yyerror(& (yylsp[-2]), state,
			 "result.color[] exceeds MAX_DRAW_BUFFERS_ARB");
		 YYERROR;
	      }
	      (yyval.integer) = FRAG_RESULT_DATA0 + (yyvsp[-1].integer);
	   }
	}
#line 4157 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 248:
#line 1930 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.integer) = VARYING_SLOT_COL0;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4170 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 249:
#line 1939 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.integer) = VARYING_SLOT_BFC0;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4183 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 250:
#line 1950 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   (yyval.integer) = 0; 
	}
#line 4191 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 251:
#line 1954 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.integer) = 0;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4204 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 252:
#line 1963 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if (state->mode == ARB_vertex) {
	      (yyval.integer) = 1;
	   } else {
	      yyerror(& (yylsp[0]), state, "invalid program result name");
	      YYERROR;
	   }
	}
#line 4217 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 253:
#line 1973 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4223 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 254:
#line 1974 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4229 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 255:
#line 1975 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 1; }
#line 4235 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 256:
#line 1978 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4241 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 257:
#line 1979 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4247 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 258:
#line 1980 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 1; }
#line 4253 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 259:
#line 1983 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4259 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 260:
#line 1984 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = (yyvsp[-1].integer); }
#line 4265 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 261:
#line 1987 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4271 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 262:
#line 1988 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = (yyvsp[-1].integer); }
#line 4277 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 263:
#line 1991 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = 0; }
#line 4283 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 264:
#line 1992 "./program/program_parse.y" /* yacc.c:1646  */
    { (yyval.integer) = (yyvsp[-1].integer); }
#line 4289 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 265:
#line 1996 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->MaxTextureCoordUnits) {
	      yyerror(& (yylsp[0]), state, "invalid texture coordinate unit selector");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 4302 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 266:
#line 2007 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->MaxTextureImageUnits) {
	      yyerror(& (yylsp[0]), state, "invalid texture image unit selector");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 4315 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 267:
#line 2018 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   if ((unsigned) (yyvsp[0].integer) >= state->MaxTextureUnits) {
	      yyerror(& (yylsp[0]), state, "invalid texture unit selector");
	      YYERROR;
	   }

	   (yyval.integer) = (yyvsp[0].integer);
	}
#line 4328 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;

  case 268:
#line 2029 "./program/program_parse.y" /* yacc.c:1646  */
    {
	   struct asm_symbol *exist = (struct asm_symbol *)
              _mesa_symbol_table_find_symbol(state->st, (yyvsp[-2].string));
	   struct asm_symbol *target = (struct asm_symbol *)
              _mesa_symbol_table_find_symbol(state->st, (yyvsp[0].string));

	   free((yyvsp[0].string));

	   if (exist != NULL) {
	      char m[1000];
	      _mesa_snprintf(m, sizeof(m), "redeclared identifier: %s", (yyvsp[-2].string));
	      free((yyvsp[-2].string));
	      yyerror(& (yylsp[-2]), state, m);
	      YYERROR;
	   } else if (target == NULL) {
	      free((yyvsp[-2].string));
	      yyerror(& (yylsp[0]), state,
		      "undefined variable binding in ALIAS statement");
	      YYERROR;
	   } else {
              _mesa_symbol_table_add_symbol(state->st, (yyvsp[-2].string), target);
	   }
	}
#line 4356 "program/program_parse.tab.c" /* yacc.c:1646  */
    break;


#line 4360 "program/program_parse.tab.c" /* yacc.c:1646  */
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
#line 2058 "./program/program_parse.y" /* yacc.c:1906  */


void
asm_instruction_set_operands(struct asm_instruction *inst,
			     const struct prog_dst_register *dst,
			     const struct asm_src_register *src0,
			     const struct asm_src_register *src1,
			     const struct asm_src_register *src2)
{
   /* In the core ARB extensions only the KIL instruction doesn't have a
    * destination register.
    */
   if (dst == NULL) {
      init_dst_reg(& inst->Base.DstReg);
   } else {
      inst->Base.DstReg = *dst;
   }

   if (src0 != NULL) {
      inst->Base.SrcReg[0] = src0->Base;
      inst->SrcReg[0] = *src0;
   } else {
      init_src_reg(& inst->SrcReg[0]);
   }

   if (src1 != NULL) {
      inst->Base.SrcReg[1] = src1->Base;
      inst->SrcReg[1] = *src1;
   } else {
      init_src_reg(& inst->SrcReg[1]);
   }

   if (src2 != NULL) {
      inst->Base.SrcReg[2] = src2->Base;
      inst->SrcReg[2] = *src2;
   } else {
      init_src_reg(& inst->SrcReg[2]);
   }
}


struct asm_instruction *
asm_instruction_ctor(enum prog_opcode op,
		     const struct prog_dst_register *dst,
		     const struct asm_src_register *src0,
		     const struct asm_src_register *src1,
		     const struct asm_src_register *src2)
{
   struct asm_instruction *inst = CALLOC_STRUCT(asm_instruction);

   if (inst) {
      _mesa_init_instructions(& inst->Base, 1);
      inst->Base.Opcode = op;

      asm_instruction_set_operands(inst, dst, src0, src1, src2);
   }

   return inst;
}


struct asm_instruction *
asm_instruction_copy_ctor(const struct prog_instruction *base,
			  const struct prog_dst_register *dst,
			  const struct asm_src_register *src0,
			  const struct asm_src_register *src1,
			  const struct asm_src_register *src2)
{
   struct asm_instruction *inst = CALLOC_STRUCT(asm_instruction);

   if (inst) {
      _mesa_init_instructions(& inst->Base, 1);
      inst->Base.Opcode = base->Opcode;
      inst->Base.Saturate = base->Saturate;

      asm_instruction_set_operands(inst, dst, src0, src1, src2);
   }

   return inst;
}


void
init_dst_reg(struct prog_dst_register *r)
{
   memset(r, 0, sizeof(*r));
   r->File = PROGRAM_UNDEFINED;
   r->WriteMask = WRITEMASK_XYZW;
}


/** Like init_dst_reg() but set the File and Index fields. */
void
set_dst_reg(struct prog_dst_register *r, gl_register_file file, GLint index)
{
   const GLint maxIndex = 1 << INST_INDEX_BITS;
   const GLint minIndex = 0;
   assert(index >= minIndex);
   (void) minIndex;
   assert(index <= maxIndex);
   (void) maxIndex;
   assert(file == PROGRAM_TEMPORARY ||
	  file == PROGRAM_ADDRESS ||
	  file == PROGRAM_OUTPUT);
   memset(r, 0, sizeof(*r));
   r->File = file;
   r->Index = index;
   r->WriteMask = WRITEMASK_XYZW;
}


void
init_src_reg(struct asm_src_register *r)
{
   memset(r, 0, sizeof(*r));
   r->Base.File = PROGRAM_UNDEFINED;
   r->Base.Swizzle = SWIZZLE_NOOP;
   r->Symbol = NULL;
}


/** Like init_src_reg() but set the File and Index fields.
 * \return GL_TRUE if a valid src register, GL_FALSE otherwise
 */
void
set_src_reg(struct asm_src_register *r, gl_register_file file, GLint index)
{
   set_src_reg_swz(r, file, index, SWIZZLE_XYZW);
}


void
set_src_reg_swz(struct asm_src_register *r, gl_register_file file, GLint index,
                GLuint swizzle)
{
   const GLint maxIndex = (1 << INST_INDEX_BITS) - 1;
   const GLint minIndex = -(1 << INST_INDEX_BITS);
   assert(file < PROGRAM_FILE_MAX);
   assert(index >= minIndex);
   (void) minIndex;
   assert(index <= maxIndex);
   (void) maxIndex;
   memset(r, 0, sizeof(*r));
   r->Base.File = file;
   r->Base.Index = index;
   r->Base.Swizzle = swizzle;
   r->Symbol = NULL;
}


/**
 * Validate the set of inputs used by a program
 *
 * Validates that legal sets of inputs are used by the program.  In this case
 * "used" included both reading the input or binding the input to a name using
 * the \c ATTRIB command.
 *
 * \return
 * \c TRUE if the combination of inputs used is valid, \c FALSE otherwise.
 */
int
validate_inputs(struct YYLTYPE *locp, struct asm_parser_state *state)
{
   const GLbitfield64 inputs = state->prog->info.inputs_read | state->InputsBound;

   if (((inputs & VERT_BIT_FF_ALL) & (inputs >> VERT_ATTRIB_GENERIC0)) != 0) {
      yyerror(locp, state, "illegal use of generic attribute and name attribute");
      return 0;
   }

   return 1;
}


struct asm_symbol *
declare_variable(struct asm_parser_state *state, char *name, enum asm_type t,
		 struct YYLTYPE *locp)
{
   struct asm_symbol *s = NULL;
   struct asm_symbol *exist = (struct asm_symbol *)
      _mesa_symbol_table_find_symbol(state->st, name);


   if (exist != NULL) {
      yyerror(locp, state, "redeclared identifier");
   } else {
      s = calloc(1, sizeof(struct asm_symbol));
      s->name = name;
      s->type = t;

      switch (t) {
      case at_temp:
         if (state->prog->arb.NumTemporaries >= state->limits->MaxTemps) {
	    yyerror(locp, state, "too many temporaries declared");
	    free(s);
	    return NULL;
	 }

         s->temp_binding = state->prog->arb.NumTemporaries;
         state->prog->arb.NumTemporaries++;
	 break;

      case at_address:
         if (state->prog->arb.NumAddressRegs >=
             state->limits->MaxAddressRegs) {
	    yyerror(locp, state, "too many address registers declared");
	    free(s);
	    return NULL;
	 }

	 /* FINISHME: Add support for multiple address registers.
	  */
         state->prog->arb.NumAddressRegs++;
	 break;

      default:
	 break;
      }

      _mesa_symbol_table_add_symbol(state->st, s->name, s);
      s->next = state->sym;
      state->sym = s;
   }

   return s;
}


int add_state_reference(struct gl_program_parameter_list *param_list,
			const gl_state_index tokens[STATE_LENGTH])
{
   const GLuint size = 4; /* XXX fix */
   char *name;
   GLint index;

   name = _mesa_program_state_string(tokens);
   index = _mesa_add_parameter(param_list, PROGRAM_STATE_VAR, name,
                               size, GL_NONE, NULL, tokens);
   param_list->StateFlags |= _mesa_program_state_flags(tokens);

   /* free name string here since we duplicated it in add_parameter() */
   free(name);

   return index;
}


int
initialize_symbol_from_state(struct gl_program *prog,
			     struct asm_symbol *param_var, 
			     const gl_state_index tokens[STATE_LENGTH])
{
   int idx = -1;
   gl_state_index state_tokens[STATE_LENGTH];


   memcpy(state_tokens, tokens, sizeof(state_tokens));

   param_var->type = at_param;
   param_var->param_binding_type = PROGRAM_STATE_VAR;

   /* If we are adding a STATE_MATRIX that has multiple rows, we need to
    * unroll it and call add_state_reference() for each row
    */
   if ((state_tokens[0] == STATE_MODELVIEW_MATRIX ||
	state_tokens[0] == STATE_PROJECTION_MATRIX ||
	state_tokens[0] == STATE_MVP_MATRIX ||
	state_tokens[0] == STATE_TEXTURE_MATRIX ||
	state_tokens[0] == STATE_PROGRAM_MATRIX)
       && (state_tokens[2] != state_tokens[3])) {
      int row;
      const int first_row = state_tokens[2];
      const int last_row = state_tokens[3];

      for (row = first_row; row <= last_row; row++) {
	 state_tokens[2] = state_tokens[3] = row;

	 idx = add_state_reference(prog->Parameters, state_tokens);
	 if (param_var->param_binding_begin == ~0U) {
	    param_var->param_binding_begin = idx;
            param_var->param_binding_swizzle = SWIZZLE_XYZW;
         }

	 param_var->param_binding_length++;
      }
   }
   else {
      idx = add_state_reference(prog->Parameters, state_tokens);
      if (param_var->param_binding_begin == ~0U) {
	 param_var->param_binding_begin = idx;
         param_var->param_binding_swizzle = SWIZZLE_XYZW;
      }
      param_var->param_binding_length++;
   }

   return idx;
}


int
initialize_symbol_from_param(struct gl_program *prog,
			     struct asm_symbol *param_var, 
			     const gl_state_index tokens[STATE_LENGTH])
{
   int idx = -1;
   gl_state_index state_tokens[STATE_LENGTH];


   memcpy(state_tokens, tokens, sizeof(state_tokens));

   assert((state_tokens[0] == STATE_VERTEX_PROGRAM)
	  || (state_tokens[0] == STATE_FRAGMENT_PROGRAM));
   assert((state_tokens[1] == STATE_ENV)
	  || (state_tokens[1] == STATE_LOCAL));

   /*
    * The param type is STATE_VAR.  The program parameter entry will
    * effectively be a pointer into the LOCAL or ENV parameter array.
    */
   param_var->type = at_param;
   param_var->param_binding_type = PROGRAM_STATE_VAR;

   /* If we are adding a STATE_ENV or STATE_LOCAL that has multiple elements,
    * we need to unroll it and call add_state_reference() for each row
    */
   if (state_tokens[2] != state_tokens[3]) {
      int row;
      const int first_row = state_tokens[2];
      const int last_row = state_tokens[3];

      for (row = first_row; row <= last_row; row++) {
	 state_tokens[2] = state_tokens[3] = row;

	 idx = add_state_reference(prog->Parameters, state_tokens);
	 if (param_var->param_binding_begin == ~0U) {
	    param_var->param_binding_begin = idx;
            param_var->param_binding_swizzle = SWIZZLE_XYZW;
         }
	 param_var->param_binding_length++;
      }
   }
   else {
      idx = add_state_reference(prog->Parameters, state_tokens);
      if (param_var->param_binding_begin == ~0U) {
	 param_var->param_binding_begin = idx;
         param_var->param_binding_swizzle = SWIZZLE_XYZW;
      }
      param_var->param_binding_length++;
   }

   return idx;
}


/**
 * Put a float/vector constant/literal into the parameter list.
 * \param param_var  returns info about the parameter/constant's location,
 *                   binding, type, etc.
 * \param vec  the vector/constant to add
 * \param allowSwizzle  if true, try to consolidate constants which only differ
 *                      by a swizzle.  We don't want to do this when building
 *                      arrays of constants that may be indexed indirectly.
 * \return index of the constant in the parameter list.
 */
int
initialize_symbol_from_const(struct gl_program *prog,
			     struct asm_symbol *param_var, 
			     const struct asm_vector *vec,
                             GLboolean allowSwizzle)
{
   unsigned swizzle;
   const int idx = _mesa_add_unnamed_constant(prog->Parameters,
                                              vec->data, vec->count,
                                              allowSwizzle ? &swizzle : NULL);

   param_var->type = at_param;
   param_var->param_binding_type = PROGRAM_CONSTANT;

   if (param_var->param_binding_begin == ~0U) {
      param_var->param_binding_begin = idx;
      param_var->param_binding_swizzle = allowSwizzle ? swizzle : SWIZZLE_XYZW;
   }
   param_var->param_binding_length++;

   return idx;
}


char *
make_error_string(const char *fmt, ...)
{
   int length;
   char *str;
   va_list args;


   /* Call vsnprintf once to determine how large the final string is.  Call it
    * again to do the actual formatting.  from the vsnprintf manual page:
    *
    *    Upon successful return, these functions return the number of
    *    characters printed  (not including the trailing '\0' used to end
    *    output to strings).
    */
   va_start(args, fmt);
   length = 1 + vsnprintf(NULL, 0, fmt, args);
   va_end(args);

   str = malloc(length);
   if (str) {
      va_start(args, fmt);
      vsnprintf(str, length, fmt, args);
      va_end(args);
   }

   return str;
}


void
yyerror(YYLTYPE *locp, struct asm_parser_state *state, const char *s)
{
   char *err_str;


   err_str = make_error_string("glProgramStringARB(%s)\n", s);
   if (err_str) {
      _mesa_error(state->ctx, GL_INVALID_OPERATION, "%s", err_str);
      free(err_str);
   }

   err_str = make_error_string("line %u, char %u: error: %s\n",
			       locp->first_line, locp->first_column, s);
   _mesa_set_program_error(state->ctx, locp->position, err_str);

   if (err_str) {
      free(err_str);
   }
}


GLboolean
_mesa_parse_arb_program(struct gl_context *ctx, GLenum target, const GLubyte *str,
			GLsizei len, struct asm_parser_state *state)
{
   struct asm_instruction *inst;
   unsigned i;
   GLubyte *strz;
   GLboolean result = GL_FALSE;
   void *temp;
   struct asm_symbol *sym;

   state->ctx = ctx;
   state->prog->Target = target;
   state->prog->Parameters = _mesa_new_parameter_list();

   /* Make a copy of the program string and force it to be NUL-terminated.
    */
   strz = (GLubyte *) ralloc_size(state->mem_ctx, len + 1);
   if (strz == NULL) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glProgramStringARB");
      return GL_FALSE;
   }
   memcpy (strz, str, len);
   strz[len] = '\0';

   state->prog->String = strz;

   state->st = _mesa_symbol_table_ctor();

   state->limits = (target == GL_VERTEX_PROGRAM_ARB)
      ? & ctx->Const.Program[MESA_SHADER_VERTEX]
      : & ctx->Const.Program[MESA_SHADER_FRAGMENT];

   state->MaxTextureImageUnits = ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits;
   state->MaxTextureCoordUnits = ctx->Const.MaxTextureCoordUnits;
   state->MaxTextureUnits = ctx->Const.MaxTextureUnits;
   state->MaxClipPlanes = ctx->Const.MaxClipPlanes;
   state->MaxLights = ctx->Const.MaxLights;
   state->MaxProgramMatrices = ctx->Const.MaxProgramMatrices;
   state->MaxDrawBuffers = ctx->Const.MaxDrawBuffers;

   state->state_param_enum = (target == GL_VERTEX_PROGRAM_ARB)
      ? STATE_VERTEX_PROGRAM : STATE_FRAGMENT_PROGRAM;

   _mesa_set_program_error(ctx, -1, NULL);

   _mesa_program_lexer_ctor(& state->scanner, state, (const char *) str, len);
   yyparse(state);
   _mesa_program_lexer_dtor(state->scanner);


   if (ctx->Program.ErrorPos != -1) {
      goto error;
   }

   if (! _mesa_layout_parameters(state)) {
      struct YYLTYPE loc;

      loc.first_line = 0;
      loc.first_column = 0;
      loc.position = len;

      yyerror(& loc, state, "invalid PARAM usage");
      goto error;
   }


   
   /* Add one instruction to store the "END" instruction.
    */
   state->prog->arb.Instructions =
      rzalloc_array(state->mem_ctx, struct prog_instruction,
                    state->prog->arb.NumInstructions + 1);

   if (state->prog->arb.Instructions == NULL) {
      goto error;
   }

   inst = state->inst_head;
   for (i = 0; i < state->prog->arb.NumInstructions; i++) {
      struct asm_instruction *const temp = inst->next;

      state->prog->arb.Instructions[i] = inst->Base;
      inst = temp;
   }

   /* Finally, tag on an OPCODE_END instruction */
   {
      const GLuint numInst = state->prog->arb.NumInstructions;
      _mesa_init_instructions(state->prog->arb.Instructions + numInst, 1);
      state->prog->arb.Instructions[numInst].Opcode = OPCODE_END;
   }
   state->prog->arb.NumInstructions++;

   state->prog->arb.NumParameters = state->prog->Parameters->NumParameters;
   state->prog->arb.NumAttributes =
      _mesa_bitcount_64(state->prog->info.inputs_read);

   /*
    * Initialize native counts to logical counts.  The device driver may
    * change them if program is translated into a hardware program.
    */
   state->prog->arb.NumNativeInstructions = state->prog->arb.NumInstructions;
   state->prog->arb.NumNativeTemporaries = state->prog->arb.NumTemporaries;
   state->prog->arb.NumNativeParameters = state->prog->arb.NumParameters;
   state->prog->arb.NumNativeAttributes = state->prog->arb.NumAttributes;
   state->prog->arb.NumNativeAddressRegs = state->prog->arb.NumAddressRegs;

   result = GL_TRUE;

error:
   for (inst = state->inst_head; inst != NULL; inst = temp) {
      temp = inst->next;
      free(inst);
   }

   state->inst_head = NULL;
   state->inst_tail = NULL;

   for (sym = state->sym; sym != NULL; sym = temp) {
      temp = sym->next;

      free((void *) sym->name);
      free(sym);
   }
   state->sym = NULL;

   _mesa_symbol_table_dtor(state->st);
   state->st = NULL;

   return result;
}
