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
#line 98 "/home/emil/development/mesa/src/compiler/glsl/glsl_parser.yy" /* yacc.c:1909  */

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

#line 338 "./glsl/glsl_parser.h" /* yacc.c:1909  */
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
