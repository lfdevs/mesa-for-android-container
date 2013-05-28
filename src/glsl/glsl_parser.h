/* A Bison parser, made by GNU Bison 2.6.1.  */

/* Bison interface for Yacc-like parsers in C
   
      Copyright (C) 1984, 1989-1990, 2000-2012 Free Software Foundation, Inc.
   
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

#ifndef _MESA_GLSL_SRC_GLSL_GLSL_PARSER_H
# define _MESA_GLSL_SRC_GLSL_GLSL_PARSER_H
/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int _mesa_glsl_debug;
#endif

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     ATTRIBUTE = 258,
     CONST_TOK = 259,
     BOOL_TOK = 260,
     FLOAT_TOK = 261,
     INT_TOK = 262,
     UINT_TOK = 263,
     BREAK = 264,
     CONTINUE = 265,
     DO = 266,
     ELSE = 267,
     FOR = 268,
     IF = 269,
     DISCARD = 270,
     RETURN = 271,
     SWITCH = 272,
     CASE = 273,
     DEFAULT = 274,
     BVEC2 = 275,
     BVEC3 = 276,
     BVEC4 = 277,
     IVEC2 = 278,
     IVEC3 = 279,
     IVEC4 = 280,
     UVEC2 = 281,
     UVEC3 = 282,
     UVEC4 = 283,
     VEC2 = 284,
     VEC3 = 285,
     VEC4 = 286,
     CENTROID = 287,
     IN_TOK = 288,
     OUT_TOK = 289,
     INOUT_TOK = 290,
     UNIFORM = 291,
     VARYING = 292,
     NOPERSPECTIVE = 293,
     FLAT = 294,
     SMOOTH = 295,
     MAT2X2 = 296,
     MAT2X3 = 297,
     MAT2X4 = 298,
     MAT3X2 = 299,
     MAT3X3 = 300,
     MAT3X4 = 301,
     MAT4X2 = 302,
     MAT4X3 = 303,
     MAT4X4 = 304,
     SAMPLER1D = 305,
     SAMPLER2D = 306,
     SAMPLER3D = 307,
     SAMPLERCUBE = 308,
     SAMPLER1DSHADOW = 309,
     SAMPLER2DSHADOW = 310,
     SAMPLERCUBESHADOW = 311,
     SAMPLER1DARRAY = 312,
     SAMPLER2DARRAY = 313,
     SAMPLER1DARRAYSHADOW = 314,
     SAMPLER2DARRAYSHADOW = 315,
     SAMPLERCUBEARRAY = 316,
     SAMPLERCUBEARRAYSHADOW = 317,
     ISAMPLER1D = 318,
     ISAMPLER2D = 319,
     ISAMPLER3D = 320,
     ISAMPLERCUBE = 321,
     ISAMPLER1DARRAY = 322,
     ISAMPLER2DARRAY = 323,
     ISAMPLERCUBEARRAY = 324,
     USAMPLER1D = 325,
     USAMPLER2D = 326,
     USAMPLER3D = 327,
     USAMPLERCUBE = 328,
     USAMPLER1DARRAY = 329,
     USAMPLER2DARRAY = 330,
     USAMPLERCUBEARRAY = 331,
     SAMPLER2DRECT = 332,
     ISAMPLER2DRECT = 333,
     USAMPLER2DRECT = 334,
     SAMPLER2DRECTSHADOW = 335,
     SAMPLERBUFFER = 336,
     ISAMPLERBUFFER = 337,
     USAMPLERBUFFER = 338,
     SAMPLEREXTERNALOES = 339,
     STRUCT = 340,
     VOID_TOK = 341,
     WHILE = 342,
     IDENTIFIER = 343,
     TYPE_IDENTIFIER = 344,
     NEW_IDENTIFIER = 345,
     FLOATCONSTANT = 346,
     INTCONSTANT = 347,
     UINTCONSTANT = 348,
     BOOLCONSTANT = 349,
     FIELD_SELECTION = 350,
     LEFT_OP = 351,
     RIGHT_OP = 352,
     INC_OP = 353,
     DEC_OP = 354,
     LE_OP = 355,
     GE_OP = 356,
     EQ_OP = 357,
     NE_OP = 358,
     AND_OP = 359,
     OR_OP = 360,
     XOR_OP = 361,
     MUL_ASSIGN = 362,
     DIV_ASSIGN = 363,
     ADD_ASSIGN = 364,
     MOD_ASSIGN = 365,
     LEFT_ASSIGN = 366,
     RIGHT_ASSIGN = 367,
     AND_ASSIGN = 368,
     XOR_ASSIGN = 369,
     OR_ASSIGN = 370,
     SUB_ASSIGN = 371,
     INVARIANT = 372,
     LOWP = 373,
     MEDIUMP = 374,
     HIGHP = 375,
     SUPERP = 376,
     PRECISION = 377,
     VERSION_TOK = 378,
     EXTENSION = 379,
     LINE = 380,
     COLON = 381,
     EOL = 382,
     INTERFACE = 383,
     OUTPUT = 384,
     PRAGMA_DEBUG_ON = 385,
     PRAGMA_DEBUG_OFF = 386,
     PRAGMA_OPTIMIZE_ON = 387,
     PRAGMA_OPTIMIZE_OFF = 388,
     PRAGMA_INVARIANT_ALL = 389,
     LAYOUT_TOK = 390,
     ASM = 391,
     CLASS = 392,
     UNION = 393,
     ENUM = 394,
     TYPEDEF = 395,
     TEMPLATE = 396,
     THIS = 397,
     PACKED_TOK = 398,
     GOTO = 399,
     INLINE_TOK = 400,
     NOINLINE = 401,
     VOLATILE = 402,
     PUBLIC_TOK = 403,
     STATIC = 404,
     EXTERN = 405,
     EXTERNAL = 406,
     LONG_TOK = 407,
     SHORT_TOK = 408,
     DOUBLE_TOK = 409,
     HALF = 410,
     FIXED_TOK = 411,
     UNSIGNED = 412,
     INPUT_TOK = 413,
     OUPTUT = 414,
     HVEC2 = 415,
     HVEC3 = 416,
     HVEC4 = 417,
     DVEC2 = 418,
     DVEC3 = 419,
     DVEC4 = 420,
     FVEC2 = 421,
     FVEC3 = 422,
     FVEC4 = 423,
     SAMPLER3DRECT = 424,
     SIZEOF = 425,
     CAST = 426,
     NAMESPACE = 427,
     USING = 428,
     COHERENT = 429,
     RESTRICT = 430,
     READONLY = 431,
     WRITEONLY = 432,
     RESOURCE = 433,
     ATOMIC_UINT = 434,
     PATCH = 435,
     SAMPLE = 436,
     SUBROUTINE = 437,
     SAMPLER2DMS = 438,
     ISAMPLER2DMS = 439,
     USAMPLER2DMS = 440,
     SAMPLER2DMSARRAY = 441,
     ISAMPLER2DMSARRAY = 442,
     USAMPLER2DMSARRAY = 443,
     ERROR_TOK = 444,
     COMMON = 445,
     PARTITION = 446,
     ACTIVE = 447,
     FILTER = 448,
     IMAGE1D = 449,
     IMAGE2D = 450,
     IMAGE3D = 451,
     IMAGECUBE = 452,
     IMAGE1DARRAY = 453,
     IMAGE2DARRAY = 454,
     IIMAGE1D = 455,
     IIMAGE2D = 456,
     IIMAGE3D = 457,
     IIMAGECUBE = 458,
     IIMAGE1DARRAY = 459,
     IIMAGE2DARRAY = 460,
     UIMAGE1D = 461,
     UIMAGE2D = 462,
     UIMAGE3D = 463,
     UIMAGECUBE = 464,
     UIMAGE1DARRAY = 465,
     UIMAGE2DARRAY = 466,
     IMAGE1DSHADOW = 467,
     IMAGE2DSHADOW = 468,
     IMAGEBUFFER = 469,
     IIMAGEBUFFER = 470,
     UIMAGEBUFFER = 471,
     IMAGE1DARRAYSHADOW = 472,
     IMAGE2DARRAYSHADOW = 473,
     ROW_MAJOR = 474
   };
#endif


#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{
/* Line 2049 of yacc.c  */
#line 59 "glsl_parser.yy"

   int n;
   float real;
   const char *identifier;

   struct ast_type_qualifier type_qualifier;

   ast_node *node;
   ast_type_specifier *type_specifier;
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
   ast_uniform_block *uniform_block;

   struct {
      ast_node *cond;
      ast_expression *rest;
   } for_rest_statement;

   struct {
      ast_node *then_statement;
      ast_node *else_statement;
   } selection_rest_statement;


/* Line 2049 of yacc.c  */
#line 313 "../../src/glsl/glsl_parser.h"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int _mesa_glsl_parse (void *YYPARSE_PARAM);
#else
int _mesa_glsl_parse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int _mesa_glsl_parse (struct _mesa_glsl_parse_state *state);
#else
int _mesa_glsl_parse ();
#endif
#endif /* ! YYPARSE_PARAM */

#endif /* !_MESA_GLSL_SRC_GLSL_GLSL_PARSER_H  */
