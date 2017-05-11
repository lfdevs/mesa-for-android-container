/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

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
#line 98 "./glsl/glsl_parser.yy" /* yacc.c:1909  */

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

#line 349 "./glsl/glsl_parser.h" /* yacc.c:1909  */
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
