
#include "nir.h"
#include "nir_search.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search111_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search111_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search111_0.value, &search111_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace111_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace111_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search122_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search122_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search122_0.value, &search122_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace122 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search123_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search123_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search123 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search123_0.value, &search123_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace123 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search124_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search124_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search124 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search124_0.value, &search124_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace124 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search132_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search132_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search132_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search132_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search132_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search132_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search132_0.value, &search132_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace132_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace132_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace132_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace132_0_0.value, &replace132_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace132_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search140_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search140_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search140_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search140_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search140_1_0.value, &search140_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search140 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search140_0.value, &search140_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace140_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace140_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace140 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace140_0.value, &replace140_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search141_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search141_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search141_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search141_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search141_1_0.value, &search141_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search141 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search141_0.value, &search141_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace141_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace141_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace141 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace141_0.value, &replace141_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search190_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search190_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search190_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search190_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search190_1_0.value, &search190_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search190_0.value, &search190_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace190_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace190_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace190_0.value, &replace190_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search191_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search191_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search191_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search191_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search191_1_0.value, &search191_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search191_0.value, &search191_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace191_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace191_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace191_0.value, &replace191_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search192_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search192_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search192_0.value, &search192_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace192_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace192_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace192_0.value, &replace192_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search194_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search194_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search194_0.value, &search194_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace194_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace194_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace194_0.value, &replace194_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search236_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search236_0_0.value, &search236_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search236_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search236_1_0.value, &search236_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search236_0.value, &search236_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace236_0.value, &replace236_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search237_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search237_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search237_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search237_0_0.value, &search237_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search237_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search237_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search237_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search237_1_0.value, &search237_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search237_0.value, &search237_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace237_0.value, &replace237_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search238_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search238_0_0.value, &search238_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search238_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search238_1_0.value, &search238_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search238_0.value, &search238_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace238_0.value, &replace238_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search239_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search239_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search239_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search239_0_0.value, &search239_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search239_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search239_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search239_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search239_1_0.value, &search239_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search239_0.value, &search239_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace239_0.value, &replace239_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search240_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search240_0_0.value, &search240_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search240_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search240_1_0.value, &search240_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search240_0.value, &search240_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace240_0.value, &replace240_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search241_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search241_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search241_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search241_0_0.value, &search241_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search241_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search241_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search241_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search241_1_0.value, &search241_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search241_0.value, &search241_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace241_0.value, &replace241_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search111, &replace111.value, 0 },
   { &search122, &replace122.value, 0 },
   { &search123, &replace123.value, 0 },
   { &search124, &replace124.value, 0 },
   { &search132, &replace132.value, 0 },
   { &search140, &replace140.value, 0 },
   { &search141, &replace141.value, 0 },
   { &search190, &replace190.value, 17 },
   { &search191, &replace191.value, 17 },
   { &search192, &replace192.value, 17 },
   { &search194, &replace194.value, 18 },
   { &search236, &replace236.value, 0 },
   { &search237, &replace237.value, 0 },
   { &search238, &replace238.value, 0 },
   { &search239, &replace239.value, 0 },
   { &search240, &replace240.value, 0 },
   { &search241, &replace241.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search182_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search182_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search182_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search182_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace182 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search185_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search185_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search185_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search185_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace185_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace185_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search186_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search186_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search186_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search186_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace186_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace186_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_i2b_xforms[] = {
   { &search182, &replace182.value, 0 },
   { &search185, &replace185.value, 0 },
   { &search186, &replace186.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search129_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search129_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search129 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search129_0.value, &search129_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace129 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search130_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search130_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search130 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search130_0.value, &search130_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace130 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search129, &replace129.value, 0 },
   { &search130, &replace130.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search104_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search104_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search104_0.value, &search104_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace104_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace104_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace104_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace104_0_0.value, &replace104_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace104_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_seq_xforms[] = {
   { &search104, &replace104.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search115_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search115_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search115_0.value, &search115_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace115 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search250_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search250_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search250_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search250_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search250_0_0.value, &search250_0_1.value, &search250_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search250_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search250_0.value, &search250_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace250_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace250_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace250_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace250_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace250_1_0.value, &replace250_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace250_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace250_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace250_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace250_2_0.value, &replace250_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace250_0.value, &replace250_1.value, &replace250_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search251_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search251_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search251_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search251_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search251_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search251_1_0.value, &search251_1_1.value, &search251_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search251 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search251_0.value, &search251_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace251_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace251_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace251_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace251_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace251_1_0.value, &replace251_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace251_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace251_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace251_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace251_2_0.value, &replace251_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace251 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace251_0.value, &replace251_1.value, &replace251_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search115, &replace115.value, 0 },
   { &search250, &replace250.value, 0 },
   { &search251, &replace251.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search4_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search4_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search4 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umod,
   { &search4_0.value, &search4_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace4 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search9_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search9_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search9 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umod,
   { &search9_0.value, &search9_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace9_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace9_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace9_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace9_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace9_1_0.value, &replace9_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace9 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace9_0.value, &replace9_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_umod_xforms[] = {
   { &search4, &replace4.value, 0 },
   { &search9, &replace9.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search0_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search0_0.value, &search0_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace0_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace0_0.value, &replace0_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search1_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_neg_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search1_0.value, &search1_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace1_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace1_0_1_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace1_0_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace1_0_0.value, &replace1_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace1_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search30_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search30_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search30 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search30_0.value, &search30_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace30 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search34_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search34_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search34 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search34_0.value, &search34_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace34 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search36_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search36_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search36_0.value, &search36_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace36_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace36_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search108_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search108_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search108_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search108_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search108_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search108_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search108 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search108_0.value, &search108_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace108_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace108_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace108_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace108_0_0.value, &replace108_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace108 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace108_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search208_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search208_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search208_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search208_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search208_0.value, &search208_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace208_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace208_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace208_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace208_0_0.value, &replace208_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace208_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search210_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search210_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search210_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search210_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search210_1_0.value, &search210_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search210_0.value, &search210_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace210_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace210_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace210_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace210_0_0.value, &replace210_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace210_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace210_0.value, &replace210_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search0, &replace0.value, 0 },
   { &search1, &replace1.value, 0 },
   { &search30, &replace30.value, 0 },
   { &search34, &replace34.value, 0 },
   { &search36, &replace36.value, 0 },
   { &search108, &replace108.value, 0 },
   { &search208, &replace208.value, 0 },
   { &search210, &replace210.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search120_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search120_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search120 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search120_0.value, &search120_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace120 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search260_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search260_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search260_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search260_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search260_0_0.value, &search260_0_1.value, &search260_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search260_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search260 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search260_0.value, &search260_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace260_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace260_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace260_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace260_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace260_1_0.value, &replace260_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace260_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace260_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace260_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace260_2_0.value, &replace260_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace260 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace260_0.value, &replace260_1.value, &replace260_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search261_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search261_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search261_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search261_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search261_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search261_1_0.value, &search261_1_1.value, &search261_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search261 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search261_0.value, &search261_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace261_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace261_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace261_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace261_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace261_1_0.value, &replace261_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace261_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace261_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace261_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace261_2_0.value, &replace261_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace261 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace261_0.value, &replace261_1.value, &replace261_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search120, &replace120.value, 0 },
   { &search260, &replace260.value, 0 },
   { &search261, &replace261.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search11_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search11_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search11_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search11 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search11_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace11 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search112_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search112_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search112_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search112_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace112 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search202_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search202_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace202_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace202_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace202_0.value, &replace202_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search11, &replace11.value, 0 },
   { &search112, &replace112.value, 0 },
   { &search202, &replace202.value, 20 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search29_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search29_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search29 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search29_0.value, &search29_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace29 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search33_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search33_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search33 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search33_0.value, &search33_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace33 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search35_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search35_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search35 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search35_0.value, &search35_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace35_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace35 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace35_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search109_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search109_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search109_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search109_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search109_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search109_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search109 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search109_0.value, &search109_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace109_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace109_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace109_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace109_0_0.value, &replace109_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace109 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace109_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search160_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search160_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search160_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search160_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search160_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search160_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search160 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search160_0.value, &search160_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace160_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace160_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace160_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace160_0_0.value, &replace160_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace160_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search207_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search207_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search207_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search207_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search207_0.value, &search207_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace207_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace207_0_0.value, &replace207_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace207_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search209_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search209_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search209_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search209_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search209_1_0.value, &search209_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search209 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search209_0.value, &search209_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace209_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace209_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace209_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace209_0_0.value, &replace209_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace209_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace209_0.value, &replace209_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search29, &replace29.value, 0 },
   { &search33, &replace33.value, 0 },
   { &search35, &replace35.value, 0 },
   { &search109, &replace109.value, 0 },
   { &search160, &replace160.value, 0 },
   { &search207, &replace207.value, 0 },
   { &search209, &replace209.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search220_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search220_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search220_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubitfield_extract,
   { &search220_0.value, &search220_1.value, &search220_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace220_0_0.value, &replace220_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubfe,
   { &replace220_2_0.value, &replace220_2_1.value, &replace220_2_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace220_0.value, &replace220_1.value, &replace220_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ubitfield_extract_xforms[] = {
   { &search220, &replace220.value, 26 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search37_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search37_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search37_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search37 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search37_0.value, &search37_1.value, &search37_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace37 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search38_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search38_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search38_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search38 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search38_0.value, &search38_1.value, &search38_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace38 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search39_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search39_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search39_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search39 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search39_0.value, &search39_1.value, &search39_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace39_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace39_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace39 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace39_0.value, &replace39_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search40_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search40_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search40_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search40 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search40_0.value, &search40_1.value, &search40_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace40_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace40_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace40 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace40_0.value, &replace40_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search41_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search41_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search41_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search41 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search41_0.value, &search41_1.value, &search41_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace41_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace41_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace41 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace41_0.value, &replace41_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search57_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search57_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search57_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search57 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search57_0.value, &search57_1.value, &search57_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace57_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace57_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace57_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace57_0_0.value, &replace57_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace57_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace57 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace57_0.value, &replace57_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ffma_xforms[] = {
   { &search37, &replace37.value, 0 },
   { &search38, &replace38.value, 0 },
   { &search39, &replace39.value, 0 },
   { &search40, &replace40.value, 0 },
   { &search41, &replace41.value, 0 },
   { &search57, &replace57.value, 7 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search82 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search82_0.value, &search82_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace82 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search90_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search90_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search90_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search90_0_0_0_0.value, &search90_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search90_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search90_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search90_0_0_0.value, &search90_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search90_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search90_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search90_0_0.value, &search90_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search90_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search90_0.value, &search90_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace90_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace90_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace90_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace90_0_0.value, &replace90_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace90_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace90_0.value, &replace90_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_umin_xforms[] = {
   { &search82, &replace82.value, 0 },
   { &search90, &replace90.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search83_0.value, &search83_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace83 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_umax_xforms[] = {
   { &search83, &replace83.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search73_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search73_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search73_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search73_0_0.value, &search73_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search73_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search73_2 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search73_0.value, &search73_1.value, &search73_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace73_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace73_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace73_0.value, &replace73_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search74_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search74_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search74_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search74_0_0.value, &search74_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search74_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search74_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search74_0.value, &search74_1.value, &search74_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace74_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace74_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace74_0.value, &replace74_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search75_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search75_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search75_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search75_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search75_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search75_0.value, &search75_1.value, &search75_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace75_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace75_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace75_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace75_0.value, &replace75_1.value, &replace75_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search76_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search76_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search76_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search76_1_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search76_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search76_1_0.value, &search76_1_1.value, &search76_1_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search76_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search76_0.value, &search76_1.value, &search76_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace76_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace76_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace76_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace76_0.value, &replace76_1.value, &replace76_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search77_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search77_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search77_2 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_bool32,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search77_0.value, &search77_1.value, &search77_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace77_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace77_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace77_0.value, &replace77_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search171_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search171_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search171_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search171_0.value, &search171_1.value, &search171_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace171 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search172_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search172_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search172_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search172 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search172_0.value, &search172_1.value, &search172_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace172_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace172 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace172_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search173_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search173_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search173_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search173 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search173_0.value, &search173_1.value, &search173_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace173_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace173 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace173_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search174_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search174_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search174_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search174 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search174_0.value, &search174_1.value, &search174_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace174_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace174_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace174_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace174 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace174_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search175_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search175_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search175_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search175 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search175_0.value, &search175_1.value, &search175_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace175_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace175_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace175_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace175 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace175_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search176_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search176_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search176_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search176 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search176_0.value, &search176_1.value, &search176_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace176_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace176_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace176_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace176_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace176_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace176 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace176_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search177_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search177_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search177_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search177_0.value, &search177_1.value, &search177_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace177 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search178_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search178_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search178_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search178_0.value, &search178_1.value, &search178_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace178 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search179_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search179_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search179_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search179_0.value, &search179_1.value, &search179_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace179_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace179_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace179_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace179_0_0.value, &replace179_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace179_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace179_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace179_0.value, &replace179_1.value, &replace179_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search180_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search180_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search180_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search180_0.value, &search180_1.value, &search180_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace180 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_bcsel_xforms[] = {
   { &search73, &replace73.value, 0 },
   { &search74, &replace74.value, 0 },
   { &search75, &replace75.value, 0 },
   { &search76, &replace76.value, 0 },
   { &search77, &replace77.value, 0 },
   { &search171, &replace171.value, 0 },
   { &search172, &replace172.value, 0 },
   { &search173, &replace173.value, 0 },
   { &search174, &replace174.value, 0 },
   { &search175, &replace175.value, 0 },
   { &search176, &replace176.value, 0 },
   { &search177, &replace177.value, 0 },
   { &search178, &replace178.value, 0 },
   { &search179, &replace179.value, 0 },
   { &search180, &replace180.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search103_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search103_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search103_0.value, &search103_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace103_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace103_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace103_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace103_0_0.value, &replace103_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace103_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_sge_xforms[] = {
   { &search103, &replace103.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search153_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search153_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search153_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search153 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsqrt,
   { &search153_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace153_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace153_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace153_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace153_0_0.value, &replace153_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace153_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search165_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search165_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace165_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace165_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace165_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace165_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsqrt_xforms[] = {
   { &search153, &replace153.value, 0 },
   { &search165, &replace165.value, 15 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search18_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search18_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search18 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search18_0.value, &search18_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace18 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search22_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search22_0_0.value, &search22_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search22_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search22_1_0.value, &search22_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search22 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search22_0.value, &search22_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace22_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace22_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace22_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace22_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace22_1_0.value, &replace22_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace22 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace22_0.value, &replace22_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search24_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search24_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search24_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search24_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search24 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search24_0.value, &search24_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace24 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search25_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search25_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search25_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search25_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search25_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search25_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search25_1_0.value, &search25_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search25 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search25_0.value, &search25_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace25 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search26_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search26_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search26_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search26_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search26_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search26_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search26_1_0.value, &search26_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search26 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search26_0.value, &search26_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace26 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search204_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search204_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search204_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search204_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search204_1_0.value, &search204_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search204_0.value, &search204_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace204_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace204_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace204_0.value, &replace204_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search212_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search212_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search212_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search212_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search212_1_0.value, &search212_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search212_0.value, &search212_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace212_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace212_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace212_0_0.value, &replace212_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace212_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace212_0.value, &replace212_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search18, &replace18.value, 0 },
   { &search22, &replace22.value, 0 },
   { &search24, &replace24.value, 0 },
   { &search25, &replace25.value, 0 },
   { &search26, &replace26.value, 0 },
   { &search204, &replace204.value, 0 },
   { &search212, &replace212.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search225_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_2x16,
   { &search225_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace225_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace225_0_0_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace225_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace225_0_0_0_0.value, &replace225_0_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace225_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace225_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace225_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_unorm_2x16_xforms[] = {
   { &search225, &replace225.value, 29 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search226_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_4x8,
   { &search226_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace226_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace226_0_0_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace226_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace226_0_0_0_0.value, &replace226_0_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace226_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace226_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace226_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_unorm_4x8_xforms[] = {
   { &search226, &replace226.value, 30 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search228_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_4x8,
   { &search228_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace228_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace228_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace228_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace228_0_0_0_0_1_0.value, &replace228_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace228_0_0_0_0_0.value, &replace228_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace228_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace228_0_0_0_0.value, &replace228_0_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace228_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace228_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace228_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_snorm_4x8_xforms[] = {
   { &search228, &replace228.value, 32 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search121_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search121_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fand,
   { &search121_0.value, &search121_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace121 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search121, &replace121.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search12_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search12_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search12_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search12 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search12_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace12_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace12 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace12_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search13_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search13_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search13_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search13 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search13_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace13_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace13 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace13_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search14_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search14_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &search14_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search14 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search14_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace14_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace14 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace14_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search98_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search98_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search98_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search98_0_0.value, &search98_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search98_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace98_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace98_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &replace98_0.value, &replace98_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search99_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search99_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search99_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search99_0_0.value, &search99_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search99_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace99_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace99_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &replace99_0.value, &replace99_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search100_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search100_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search100_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search100_0_0.value, &search100_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search100_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace100_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace100_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &replace100_0.value, &replace100_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search101_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search101_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search101_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search101_0_0.value, &search101_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search101_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace101_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace101_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &replace101_0.value, &replace101_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search187_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search187_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search187_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search187_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace187_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace187_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search205_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search205_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search205_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search205_0_0.value, &search205_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search205_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace205_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search12, &replace12.value, 0 },
   { &search13, &replace13.value, 0 },
   { &search14, &replace14.value, 0 },
   { &search98, &replace98.value, 0 },
   { &search99, &replace99.value, 0 },
   { &search100, &replace100.value, 0 },
   { &search101, &replace101.value, 0 },
   { &search187, &replace187.value, 0 },
   { &search205, &replace205.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search5_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search5_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search5 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imod,
   { &search5_0.value, &search5_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace5 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_imod_xforms[] = {
   { &search5, &replace5.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search117_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search117_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search117_0.value, &search117_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace117 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search167_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search167_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search167_0.value, &search167_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace167 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search170_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search170_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search170_0.value, &search170_1.value },
   (is_not_used_by_if),
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace170_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace170_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search254_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search254_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search254_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search254_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search254_0_0.value, &search254_0_1.value, &search254_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search254_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search254 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search254_0.value, &search254_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace254_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace254_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace254_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace254_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace254_1_0.value, &replace254_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace254_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace254_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace254_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace254_2_0.value, &replace254_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace254 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace254_0.value, &replace254_1.value, &replace254_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search255_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search255_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search255_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search255_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search255_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search255_1_0.value, &search255_1_1.value, &search255_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search255 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search255_0.value, &search255_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace255_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace255_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace255_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace255_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace255_1_0.value, &replace255_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace255_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace255_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace255_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace255_2_0.value, &replace255_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace255 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace255_0.value, &replace255_1.value, &replace255_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search117, &replace117.value, 0 },
   { &search167, &replace167.value, 0 },
   { &search170, &replace170.value, 0 },
   { &search254, &replace254.value, 0 },
   { &search255, &replace255.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search80_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search80_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search80 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search80_0.value, &search80_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace80 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search89_0_0_0_0.value, &search89_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search89_0_0_0.value, &search89_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search89_0_0.value, &search89_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search89_0.value, &search89_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace89_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace89_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace89_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace89_0_0.value, &replace89_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace89_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace89_0.value, &replace89_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imin_xforms[] = {
   { &search80, &replace80.value, 0 },
   { &search89, &replace89.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search155_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search155_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search155_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search155 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frsq,
   { &search155_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace155_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace155_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace155_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace155_0_0.value, &replace155_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace155 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace155_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frsq_xforms[] = {
   { &search155, &replace155.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search19_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search19_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search19 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search19_0.value, &search19_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace19 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search20_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search20_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search20 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search20_0.value, &search20_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace20 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};

static const struct transform nir_opt_algebraic_usadd_4x8_xforms[] = {
   { &search19, &replace19.value, 0 },
   { &search20, &replace20.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search3_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search3_0.value, &search3_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace3 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search7_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search7_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search7 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search7_0.value, &search7_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace7_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace7_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isign,
   { &replace7_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace7_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace7_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace7_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace7_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace7_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace7_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace7_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace7_1_0.value, &replace7_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace7 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace7_0.value, &replace7_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search8_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search8_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_neg_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search8 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search8_0.value, &search8_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace8_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isign,
   { &replace8_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace8_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace8_0_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace8_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace8_0_1_1_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace8_0_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace8_0_1_0.value, &replace8_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace8_0_0.value, &replace8_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace8_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_idiv_xforms[] = {
   { &search3, &replace3.value, 0 },
   { &search7, &replace7.value, 1 },
   { &search8, &replace8.value, 1 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search93_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search93_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search93_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search93_0_0_0.value, &search93_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search93_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search93_0_0.value, &search93_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search93_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search93_0.value, &search93_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace93_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace93_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace93_0_0.value, &replace93_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace93_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace93_0.value, &replace93_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search222_0.value, &search222_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace222_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace222_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace222_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace222_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace222_0_1_0.value, &replace222_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace222_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace222_0_0.value, &replace222_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace222_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace222_0.value, &replace222_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_u8_xforms[] = {
   { &search93, &replace93.value, 0 },
   { &search222, &replace222.value, 27 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search227_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_2x16,
   { &search227_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace227_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace227_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace227_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace227_0_0_0_0_1_0.value, &replace227_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace227_0_0_0_0_0.value, &replace227_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace227_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace227_0_0_0_0.value, &replace227_0_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace227_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace227_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace227_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_snorm_2x16_xforms[] = {
   { &search227, &replace227.value, 31 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search144_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search144_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search144 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search144_0.value, &search144_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace144_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace144_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace144_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace144_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace144_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace144_0_0.value, &replace144_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace144 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace144_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search147_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search147_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search147 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search147_0.value, &search147_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace147 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search148_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search148_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search148 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search148_0.value, &search148_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace148_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace148_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace148 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace148_0.value, &replace148_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search149_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search149_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4010000000000000 /* 4.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search149 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search149_0.value, &search149_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace149_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace149_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace149_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace149_0_0.value, &replace149_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace149_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace149_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace149_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace149_1_0.value, &replace149_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace149 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace149_0.value, &replace149_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search150_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search150_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search150 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search150_0.value, &search150_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace150_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace150 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace150_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search151_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search151_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search151_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search151_0_0.value, &search151_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search151_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search151 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search151_0.value, &search151_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace151 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search152_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search152_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search152_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search152_0_0_0.value, &search152_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search152_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search152_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search152_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search152 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search152_0.value, &search152_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace152_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace152_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search144, &replace144.value, 12 },
   { &search147, &replace147.value, 0 },
   { &search148, &replace148.value, 0 },
   { &search149, &replace149.value, 0 },
   { &search150, &replace150.value, 0 },
   { &search151, &replace151.value, 0 },
   { &search152, &replace152.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search116_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search116_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search116 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search116_0.value, &search116_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace116 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search252_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search252_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search252_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search252_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search252_0_0.value, &search252_0_1.value, &search252_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search252_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search252 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search252_0.value, &search252_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace252_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace252_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace252_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace252_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace252_1_0.value, &replace252_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace252_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace252_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace252_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace252_2_0.value, &replace252_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace252 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace252_0.value, &replace252_1.value, &replace252_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search253_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search253_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search253_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search253_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search253_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search253_1_0.value, &search253_1_1.value, &search253_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search253 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search253_0.value, &search253_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace253_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace253_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace253_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace253_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace253_1_0.value, &replace253_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace253_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace253_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace253_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace253_2_0.value, &replace253_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace253 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace253_0.value, &replace253_1.value, &replace253_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search116, &replace116.value, 0 },
   { &search252, &replace252.value, 0 },
   { &search253, &replace253.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search161_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search161_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search161 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fdiv,
   { &search161_0.value, &search161_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace161_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace161_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search162_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search162_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &search162_0.value, &search162_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace162_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace162_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace162_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace162_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace162_0.value, &replace162_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search161, &replace161.value, 0 },
   { &search162, &replace162.value, 14 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search50_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search50 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffract,
   { &search50_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace50_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace50_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace50_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace50_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace50 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace50_0.value, &replace50_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ffract_xforms[] = {
   { &search50, &replace50.value, 4 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search17_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search17_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search17 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search17_0.value, &search17_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace17 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search21_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search21_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search21_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search21_0_0.value, &search21_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search21_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search21_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search21_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search21_1_0.value, &search21_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search21 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search21_0.value, &search21_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace21_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace21_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace21_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace21_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace21_1_0.value, &replace21_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace21 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace21_0.value, &replace21_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search23_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search23_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search23_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search23_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search23 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search23_0.value, &search23_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace23 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search27_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search27_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search27_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search27_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search27_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search27_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search27_1_0.value, &search27_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search27 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search27_0.value, &search27_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace27 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search28_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search28_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search28_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search28_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search28_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search28_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search28_1_0.value, &search28_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search28 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search28_0.value, &search28_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace28 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search51_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search51_0_1_1_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search51_0_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search51_0_1_0.value, &search51_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search51_0_0.value, &search51_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search51_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search51_1_0.value, &search51_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search51_0.value, &search51_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace51_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace51_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace51_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace51 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace51_0.value, &replace51_1.value, &replace51_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search52_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search52_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search52_0_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search52_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search52_0_1_0.value, &search52_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search52_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search52_0_0.value, &search52_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search52_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search52_1_0.value, &search52_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search52 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search52_0.value, &search52_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace52_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace52_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace52_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace52 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace52_0.value, &replace52_1.value, &replace52_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search53_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search53_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search53_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search53_0_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search53_0_1_0.value, &search53_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search53_0_0.value, &search53_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search53_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search53_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search53_1_0.value, &search53_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search53_0.value, &search53_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace53_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace53_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace53_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace53 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace53_0.value, &replace53_1.value, &replace53_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search54_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search54_1_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search54_1_1_0.value, &search54_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search54_1_0.value, &search54_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search54_0.value, &search54_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace54 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace54_0.value, &replace54_1.value, &replace54_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search55_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search55_1_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search55_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search55_1_1_0.value, &search55_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search55_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search55_1_0.value, &search55_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search55 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search55_0.value, &search55_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace55_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace55_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace55_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace55 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace55_0.value, &replace55_1.value, &replace55_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search56_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search56_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search56_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search56_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search56_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search56_1_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search56_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search56_1_1_0.value, &search56_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search56_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search56_1_0.value, &search56_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search56 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search56_0.value, &search56_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace56_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace56_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace56_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace56 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace56_0.value, &replace56_1.value, &replace56_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search58_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search58_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search58_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search58_0_0.value, &search58_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search58_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search58 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search58_0.value, &search58_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace58_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace58_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace58_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace58 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &replace58_0.value, &replace58_1.value, &replace58_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search203_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search203_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search203_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search203_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search203_1_0.value, &search203_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search203 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search203_0.value, &search203_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace203_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace203_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace203_0.value, &replace203_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search211_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search211_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search211_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search211_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search211_1_0.value, &search211_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search211 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search211_0.value, &search211_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace211_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace211_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace211_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace211_0_0.value, &replace211_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace211_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace211_0.value, &replace211_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fadd_xforms[] = {
   { &search17, &replace17.value, 0 },
   { &search21, &replace21.value, 0 },
   { &search23, &replace23.value, 0 },
   { &search27, &replace27.value, 0 },
   { &search28, &replace28.value, 0 },
   { &search51, &replace51.value, 2 },
   { &search52, &replace52.value, 5 },
   { &search53, &replace53.value, 6 },
   { &search54, &replace54.value, 2 },
   { &search55, &replace55.value, 5 },
   { &search56, &replace56.value, 6 },
   { &search58, &replace58.value, 8 },
   { &search203, &replace203.value, 0 },
   { &search211, &replace211.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search59_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search59_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search59_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search59_0_0_0.value, &search59_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search59_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search59_0_0.value, &search59_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search59_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search59_0.value, &search59_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace59_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace59_0_1_0.value, &replace59_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace59_0_0.value, &replace59_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace59_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace59_1_0.value, &replace59_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace59_0.value, &replace59_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search60_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search60_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search60_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search60_0_0.value, &search60_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search60_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search60_0.value, &search60_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace60_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace60_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace60_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace60_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace60_1_0.value, &replace60_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace60_0.value, &replace60_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search134_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search134_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search134 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search134_0.value, &search134_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace134 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search135_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search135_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search135_0.value, &search135_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace135 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search59, &replace59.value, 0 },
   { &search60, &replace60.value, 0 },
   { &search134, &replace134.value, 0 },
   { &search135, &replace135.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search217_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search217_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search217 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_usub_borrow,
   { &search217_0.value, &search217_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace217_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace217_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace217_0_0.value, &replace217_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace217_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_usub_borrow_xforms[] = {
   { &search217, &replace217.value, 24 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search143_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search143_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search143_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search143 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search143_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace143 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search156_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search156_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search156_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search156 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search156_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace156_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace156_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace156_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace156_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace156_0.value, &replace156_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search157_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search157_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search157_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search157 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search157_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace157_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace157_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace157_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace157 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace157_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search158_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search158_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search158_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search158 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search158_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace158_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace158_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace158_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace158_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace158 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace158_0.value, &replace158_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search159_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search159_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search159_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search159_0_0.value, &search159_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search159 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search159_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace159_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace159_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace159_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace159_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace159 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace159_0.value, &replace159_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search143, &replace143.value, 0 },
   { &search156, &replace156.value, 0 },
   { &search157, &replace157.value, 0 },
   { &search158, &replace158.value, 0 },
   { &search159, &replace159.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search61_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search61_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search61_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search61_0_0.value, &search61_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search61 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search61_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace61_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace61_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace61_0.value, &replace61_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search62_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search62_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search62_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search62_0_0.value, &search62_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search62 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search62_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace62_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace62_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace62_0.value, &replace62_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search63_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search63_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search63_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search63_0_0.value, &search63_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search63 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search63_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace63_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace63_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace63 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace63_0.value, &replace63_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search64_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search64_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search64_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search64_0_0.value, &search64_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search64 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search64_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace64_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace64_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace64_0.value, &replace64_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search65_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search65_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search65_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search65_0_0.value, &search65_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search65_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace65_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace65_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace65_0.value, &replace65_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search66_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search66_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search66_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search66_0_0.value, &search66_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search66_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace66_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace66_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace66_0.value, &replace66_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search67_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search67_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search67_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search67_0_0.value, &search67_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search67 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search67_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace67_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace67_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace67 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace67_0.value, &replace67_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search68_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search68_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search68_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search68_0_0.value, &search68_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search68 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search68_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace68_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace68_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace68 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace68_0.value, &replace68_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search131_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search131_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search131_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search131_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace131 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_inot_xforms[] = {
   { &search61, &replace61.value, 0 },
   { &search62, &replace62.value, 0 },
   { &search63, &replace63.value, 0 },
   { &search64, &replace64.value, 0 },
   { &search65, &replace65.value, 0 },
   { &search66, &replace66.value, 0 },
   { &search67, &replace67.value, 0 },
   { &search68, &replace68.value, 0 },
   { &search131, &replace131.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search105_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search105_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search105_0.value, &search105_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace105_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace105_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace105_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace105_0_0.value, &replace105_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace105_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_sne_xforms[] = {
   { &search105, &replace105.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search213_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search213_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search213 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_fmod,
   { &search213_0.value, &search213_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace213_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace213_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace213_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace213_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace213_1_1_0_0.value, &replace213_1_1_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace213_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace213_1_0.value, &replace213_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace213_0.value, &replace213_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search214_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search214_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search214 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_fmod,
   { &search214_0.value, &search214_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace214_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace214_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace214_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace214_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace214_1_1_0_0.value, &replace214_1_1_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace214_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace214_1_0.value, &replace214_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace214_0.value, &replace214_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmod_xforms[] = {
   { &search213, &replace213.value, 21 },
   { &search214, &replace214.value, 22 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search184_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search184_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search184_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &search184_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace184_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace184_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_f2u_xforms[] = {
   { &search184, &replace184.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search181_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search181_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search181_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fcsel,
   { &search181_0.value, &search181_1.value, &search181_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace181 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_fcsel_xforms[] = {
   { &search181, &replace181.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search196_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search196_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search196_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search196_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search196_1_0.value, &search196_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search196_0.value, &search196_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace196_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace196_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace196_0.value, &replace196_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search200_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search200_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search200_0.value, &search200_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace200_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace200_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace200_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace200_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace200_0.value, &replace200_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_isub_xforms[] = {
   { &search196, &replace196.value, 0 },
   { &search200, &replace200.value, 19 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search79_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search79_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search79 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search79_0.value, &search79_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace79 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search85_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search85_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search85_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search85_0_0.value, &search85_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search85_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search85 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmax,
   { &search85_0.value, &search85_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace85_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace85_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search91_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search91_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search91_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search91_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_zero_to_one),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search91_0.value, &search91_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace91_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace91_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace91_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace91_0_0.value, &replace91_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace91_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmax_xforms[] = {
   { &search79, &replace79.value, 0 },
   { &search85, &replace85.value, 9 },
   { &search91, &replace91.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search31_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search31_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search31 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search31_0.value, &search31_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace31 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search32_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search32_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search32 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search32_0.value, &search32_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace32 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_umul_unorm_4x8_xforms[] = {
   { &search31, &replace31.value, 0 },
   { &search32, &replace32.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search218_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search218_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search218_2 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search218_3 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_insert,
   { &search218_0.value, &search218_1.value, &search218_2.value, &search218_3.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace218_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_0_1 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace218_0_0.value, &replace218_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_2_0_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_2_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace218_2_0_0.value, &replace218_2_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_2_2 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfi,
   { &replace218_2_0.value, &replace218_2_1.value, &replace218_2_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace218_0.value, &replace218_1.value, &replace218_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_bitfield_insert_xforms[] = {
   { &search218, &replace218.value, 25 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search107_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search107_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search107_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search107_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search107 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search107_0.value, &search107_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace107_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace107_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace107 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace107_0.value, &replace107_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search246_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search246_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search246_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search246_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search246_0_0.value, &search246_0_1.value, &search246_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search246_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search246_0.value, &search246_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace246_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace246_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace246_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace246_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace246_1_0.value, &replace246_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace246_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace246_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace246_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace246_2_0.value, &replace246_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace246_0.value, &replace246_1.value, &replace246_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search247_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search247_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search247_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search247_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search247_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search247_1_0.value, &search247_1_1.value, &search247_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search247_0.value, &search247_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace247_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace247_1_0.value, &replace247_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace247_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace247_2_0.value, &replace247_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace247_0.value, &replace247_1.value, &replace247_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search107, &replace107.value, 0 },
   { &search246, &replace246.value, 0 },
   { &search247, &replace247.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search42_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search42_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search42_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search42 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search42_0.value, &search42_1.value, &search42_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace42 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search43_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search43_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search43_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search43 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search43_0.value, &search43_1.value, &search43_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace43 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search44_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search44_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search44_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search44 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search44_0.value, &search44_1.value, &search44_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace44 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search45_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search45_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search45_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search45 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search45_0.value, &search45_1.value, &search45_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace45_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace45_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace45 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace45_0.value, &replace45_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search46_2_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search46_0.value, &search46_1.value, &search46_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace46_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace46_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace46_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace46 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace46_0.value, &replace46_1.value, &replace46_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search47_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search47_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search47_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search47 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search47_0.value, &search47_1.value, &search47_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace47_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace47_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace47_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace47_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace47_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace47_0_0.value, &replace47_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace47_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace47 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace47_0.value, &replace47_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search48 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_flrp,
   { &search48_0.value, &search48_1.value, &search48_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace48_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace48_0_1_0.value, &replace48_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace48_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace48_0_0.value, &replace48_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace48 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace48_0.value, &replace48_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search49_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search49_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search49_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search49 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_flrp,
   { &search49_0.value, &search49_1.value, &search49_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace49_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace49_0_1_0.value, &replace49_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace49_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace49_0_0.value, &replace49_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace49 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace49_0.value, &replace49_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flrp_xforms[] = {
   { &search42, &replace42.value, 0 },
   { &search43, &replace43.value, 0 },
   { &search44, &replace44.value, 0 },
   { &search45, &replace45.value, 0 },
   { &search46, &replace46.value, 2 },
   { &search47, &replace47.value, 0 },
   { &search48, &replace48.value, 2 },
   { &search49, &replace49.value, 3 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search94_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search94_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search94_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search94_0_0.value, &search94_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search94_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search94_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search94_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search94_1_0.value, &search94_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search94 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search94_0.value, &search94_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace94_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace94_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace94_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace94_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace94_1_0.value, &replace94_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace94_0.value, &replace94_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search95_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search95_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search95_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search95_0_0.value, &search95_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search95_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search95_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search95_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search95_1_0.value, &search95_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search95 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search95_0.value, &search95_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace95_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace95_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace95_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace95_0_0.value, &replace95_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace95_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace95_0.value, &replace95_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search96_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search96_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search96_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search96_0_0.value, &search96_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search96_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search96_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search96_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search96_1_0.value, &search96_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search96 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search96_0.value, &search96_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace96_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace96_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace96_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace96_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace96_1_0.value, &replace96_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace96_0.value, &replace96_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search97_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search97_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search97_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search97_0_0.value, &search97_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search97_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search97_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search97_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search97_1_0.value, &search97_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search97 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search97_0.value, &search97_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace97_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace97_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace97_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace97_0_0.value, &replace97_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace97_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace97_0.value, &replace97_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search125_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search125_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search125 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search125_0.value, &search125_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace125 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search126_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search126_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search126 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search126_0.value, &search126_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace126 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search127_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search127_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search127 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search127_0.value, &search127_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace127 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search133_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search133_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search133_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search133_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search133_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search133_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search133 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search133_0.value, &search133_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace133_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace133_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace133_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace133_0_0.value, &replace133_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace133 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace133_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search235_0_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search235_0_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_0_0_0_1_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0_0_0_0_1_0_0.value, &search235_0_0_0_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_0_0_0_1_0.value, &search235_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search235_0_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_1_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0_1_0_0_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_1_0_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search235_0_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_1_0_0_1_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0_1_0_0_1_0_0.value, &search235_0_0_0_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_1_0_0_1_0.value, &search235_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_1_0_0_0.value, &search235_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0_1_0_0.value, &search235_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_0_0_0_1_0.value, &search235_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0_0_0_0.value, &search235_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_0_0_0.value, &search235_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_0_0.value, &search235_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search235_0_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_0_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search235_0_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_0_0_0_1_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0_0_0_0_1_0_0.value, &search235_0_0_0_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_0_0_0_1_0.value, &search235_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0_0_0_0.value, &search235_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_0_0.value, &search235_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search235_0_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_1_0_0_0_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0_1_0_0_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_1_0_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search235_0_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_1_0_0_1_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0_1_0_0_1_0_0.value, &search235_0_0_0_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_1_0_0_1_0.value, &search235_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_1_0_0_0.value, &search235_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0_1_0_0.value, &search235_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0_0_1_0.value, &search235_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_1_0_0_0.value, &search235_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0_1_0_0.value, &search235_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_0_0_0_1_0.value, &search235_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0_0_0_0.value, &search235_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x55555555 /* 1431655765 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0_0_0.value, &search235_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_0_0.value, &search235_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search235_1_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search235_1_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_0_0_0_1_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0_0_0_0_1_0_0.value, &search235_1_0_0_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_0_0_0_1_0.value, &search235_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search235_1_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_1_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0_1_0_0_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_1_0_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search235_1_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_1_0_0_1_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0_1_0_0_1_0_0.value, &search235_1_0_0_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_1_0_0_1_0.value, &search235_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_1_0_0_0.value, &search235_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0_1_0_0.value, &search235_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_0_0_0_1_0.value, &search235_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0_0_0_0.value, &search235_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_0_0_0.value, &search235_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_0_0.value, &search235_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search235_1_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_0_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search235_1_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_0_0_0_1_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0_0_0_0_1_0_0.value, &search235_1_0_0_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_0_0_0_1_0.value, &search235_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0_0_0_0.value, &search235_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_0_0.value, &search235_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search235_1_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_1_0_0_0_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0_1_0_0_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_1_0_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search235_1_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search235_1_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_1_0_0_1_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0_1_0_0_1_0_0.value, &search235_1_0_0_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_1_0_0_1_0.value, &search235_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_1_0_0_0.value, &search235_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0_1_0_0.value, &search235_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0_0_1_0.value, &search235_1_0_0_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_1_0_0_0.value, &search235_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0_1_0_0.value, &search235_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0_0_1_0.value, &search235_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_1_0_0_0.value, &search235_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xaaaaaaaa /* 2863311530 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_1_0_0.value, &search235_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search235_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search235_1_0.value, &search235_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search235_0.value, &search235_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace235_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_reverse,
   { &replace235_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search94, &replace94.value, 0 },
   { &search95, &replace95.value, 0 },
   { &search96, &replace96.value, 0 },
   { &search97, &replace97.value, 0 },
   { &search125, &replace125.value, 0 },
   { &search126, &replace126.value, 0 },
   { &search127, &replace127.value, 0 },
   { &search133, &replace133.value, 0 },
   { &search235, &replace235.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search197_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search197_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search197_0.value, &search197_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace197 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search198_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search198_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search198_0.value, &search198_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace198 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ussub_4x8_xforms[] = {
   { &search197, &replace197.value, 0 },
   { &search198, &replace198.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search232_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_4x8,
   { &search232_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace232_1_1_0_0_0_0.value, &replace232_1_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace232_1_1_0_0_1_0.value, &replace232_1_1_0_0_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_1_1_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_1_1_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace232_1_1_0_0_2_0.value, &replace232_1_1_0_0_2_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_1_1_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_1_1_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace232_1_1_0_0_3_0.value, &replace232_1_1_0_0_3_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace232_1_1_0_0_0.value, &replace232_1_1_0_0_1.value, &replace232_1_1_0_0_2.value, &replace232_1_1_0_0_3.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f,
   { &replace232_1_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace232_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace232_1_1_0.value, &replace232_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace232_1_0.value, &replace232_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace232_0.value, &replace232_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_snorm_4x8_xforms[] = {
   { &search232, &replace232.value, 36 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search215_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search215_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frem,
   { &search215_0.value, &search215_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace215_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace215_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace215_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace215_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace215_1_1_0_0.value, &replace215_1_1_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &replace215_1_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace215_1_0.value, &replace215_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace215_0.value, &replace215_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frem_xforms[] = {
   { &search215, &replace215.value, 21 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search229_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_2x16,
   { &search229_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace229_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace229_0_0_0_0.value, &replace229_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace229_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace229_0_0_1_0.value, &replace229_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace229_0_0_0.value, &replace229_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace229_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace229_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace229_0.value, &replace229_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_unorm_2x16_xforms[] = {
   { &search229, &replace229.value, 33 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search231_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_2x16,
   { &search231_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace231_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace231_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace231_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace231_1_1_0_0_0_0.value, &replace231_1_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace231_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace231_1_1_0_0_1_0.value, &replace231_1_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace231_1_1_0_0_0.value, &replace231_1_1_0_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f,
   { &replace231_1_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace231_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace231_1_1_0.value, &replace231_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace231_1_0.value, &replace231_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace231_0.value, &replace231_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_snorm_2x16_xforms[] = {
   { &search231, &replace231.value, 35 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search81_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search81_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search81 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search81_0.value, &search81_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace81 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_imax_xforms[] = {
   { &search81, &replace81.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search86_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search86_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace86_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace86_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace86_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace86_0_0.value, &replace86_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace86_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace86_0.value, &replace86_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search87_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search87_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search87_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search87_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace87_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace87_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search110_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search110_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search110_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search110_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search110_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search110_0_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search110_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search110_0_0.value, &search110_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search110_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace110_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace110_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace110_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace110_0_0.value, &replace110_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace110_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsat_xforms[] = {
   { &search86, &replace86.value, 10 },
   { &search87, &replace87.value, 0 },
   { &search110, &replace110.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search224_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search224_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &search224_0.value, &search224_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace224_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace224_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace224_0_1_0.value, &replace224_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace224_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace224_0_0.value, &replace224_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace224_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace224_0.value, &replace224_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_u16_xforms[] = {
   { &search224, &replace224.value, 28 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search69_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search69_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search69_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search69_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search69_0.value, &search69_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace69_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace69_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search70_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search70_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search70_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search70_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search70_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search70_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search70_0.value, &search70_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace70_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace70_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search72_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search72_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search72_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search72_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search72_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search72_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search72_0.value, &search72_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace72_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace72_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace72_0.value, &replace72_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search244_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search244_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search244_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search244_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search244_0_0.value, &search244_0_1.value, &search244_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search244_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search244 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search244_0.value, &search244_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace244_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace244_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace244_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace244_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace244_1_0.value, &replace244_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace244_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace244_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace244_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace244_2_0.value, &replace244_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace244 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace244_0.value, &replace244_1.value, &replace244_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search245_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search245_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search245_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search245_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search245_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search245_1_0.value, &search245_1_1.value, &search245_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search245 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search245_0.value, &search245_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace245_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace245_1_0.value, &replace245_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace245_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace245_2_0.value, &replace245_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace245 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace245_0.value, &replace245_1.value, &replace245_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search69, &replace69.value, 0 },
   { &search70, &replace70.value, 0 },
   { &search72, &replace72.value, 0 },
   { &search244, &replace244.value, 0 },
   { &search245, &replace245.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search154_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search154_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search154_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search154 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search154_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace154_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace154_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace154_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace154 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace154_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search163_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search163_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search163_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search163 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search163_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace163 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search164_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search164_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search164_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search164 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search164_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace164_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace164_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search166_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search166_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search166_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search166 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search166_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace166_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &replace166_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search154, &replace154.value, 0 },
   { &search163, &replace163.value, 0 },
   { &search164, &replace164.value, 0 },
   { &search166, &replace166.value, 16 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search128_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search128_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search128 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fxor,
   { &search128_0.value, &search128_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace128 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search128, &replace128.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search138_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search138_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search138 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search138_0.value, &search138_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace138 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search139_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search139_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search139 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search139_0.value, &search139_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace139 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search189_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search189_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search189_0.value, &search189_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace189_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace189_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace189_0.value, &replace189_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search193_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search193_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search193_0.value, &search193_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace193_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace193_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace193_0.value, &replace193_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search138, &replace138.value, 0 },
   { &search139, &replace139.value, 0 },
   { &search189, &replace189.value, 17 },
   { &search193, &replace193.value, 18 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search142_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search142_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search142_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search142 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search142_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace142 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search145_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search145_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search145_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search145_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search145_0_0.value, &search145_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search145 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search145_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace145_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace145_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace145 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace145_0.value, &replace145_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search146_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search146_0_0_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search146_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search146_0_0_0.value, &search146_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search146_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search146_0_1_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search146_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search146_0_1_0.value, &search146_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search146_0_0.value, &search146_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search146_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace146_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace146_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace146_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace146_0_0.value, &replace146_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace146_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace146_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace146_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace146_1_0.value, &replace146_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace146 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &replace146_0.value, &replace146_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search142, &replace142.value, 0 },
   { &search145, &replace145.value, 13 },
   { &search146, &replace146.value, 13 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search136_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search136_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search136 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search136_0.value, &search136_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace136 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search137_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search137_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search137_0.value, &search137_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace137 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search136, &replace136.value, 0 },
   { &search137, &replace137.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search102_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search102_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search102_0.value, &search102_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace102_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace102_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace102_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace102_0_0.value, &replace102_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace102_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_slt_xforms[] = {
   { &search102, &replace102.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search183_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search183_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search183_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &search183_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace183_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace183_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_f2i_xforms[] = {
   { &search183, &replace183.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search71_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search71_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search71_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search71_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search71_0.value, &search71_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace71_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace71_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace71_0.value, &replace71_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search113_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search113_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search113_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search113_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search113_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search113_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search113_0.value, &search113_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace113 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search114_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search114_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search114_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search114_0_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search114_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search114_0_0.value, &search114_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search114_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search114_0.value, &search114_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace114 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search242_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search242_0_0.value, &search242_0_1.value, &search242_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search242_0.value, &search242_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace242_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace242_1_0.value, &replace242_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace242_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace242_2_0.value, &replace242_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace242_0.value, &replace242_1.value, &replace242_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search243_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search243_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search243_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search243_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search243_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search243_1_0.value, &search243_1_1.value, &search243_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search243_0.value, &search243_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace243_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace243_1_0.value, &replace243_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace243_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace243_2_0.value, &replace243_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace243_0.value, &replace243_1.value, &replace243_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search71, &replace71.value, 0 },
   { &search113, &replace113.value, 0 },
   { &search114, &replace114.value, 0 },
   { &search242, &replace242.value, 0 },
   { &search243, &replace243.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search119_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search119_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search119 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search119_0.value, &search119_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace119 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search258_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search258_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search258_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search258_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search258_0_0.value, &search258_0_1.value, &search258_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search258_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search258 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search258_0.value, &search258_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace258_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace258_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace258_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace258_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace258_1_0.value, &replace258_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace258_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace258_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace258_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace258_2_0.value, &replace258_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace258 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace258_0.value, &replace258_1.value, &replace258_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search259_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search259_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search259_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search259_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search259_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search259_1_0.value, &search259_1_1.value, &search259_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search259 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search259_0.value, &search259_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace259_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace259_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace259_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace259_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace259_1_0.value, &replace259_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace259_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace259_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace259_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace259_2_0.value, &replace259_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace259 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace259_0.value, &replace259_1.value, &replace259_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search119, &replace119.value, 0 },
   { &search258, &replace258.value, 0 },
   { &search259, &replace259.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search195_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search195_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search195_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search195_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search195_1_0.value, &search195_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search195 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsub,
   { &search195_0.value, &search195_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace195_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace195_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace195_0.value, &replace195_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search199_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search199_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search199_0.value, &search199_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace199_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace199_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace199_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace199_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace199_0.value, &replace199_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsub_xforms[] = {
   { &search195, &replace195.value, 0 },
   { &search199, &replace199.value, 19 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search10_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search10_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search10_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search10 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search10_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace10 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search201_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search201_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace201_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace201_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace201_0.value, &replace201_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search10, &replace10.value, 0 },
   { &search201, &replace201.value, 20 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search106_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search106_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search106_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search106_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search106 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search106_0.value, &search106_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace106_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace106_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace106 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace106_0.value, &replace106_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search248_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search248_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search248_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search248_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search248_0_0.value, &search248_0_1.value, &search248_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search248_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search248_0.value, &search248_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace248_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace248_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace248_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace248_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace248_1_0.value, &replace248_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace248_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace248_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace248_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace248_2_0.value, &replace248_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace248_0.value, &replace248_1.value, &replace248_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search249_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search249_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search249_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search249_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search249_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search249_1_0.value, &search249_1_1.value, &search249_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search249_0.value, &search249_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace249_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace249_1_0.value, &replace249_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace249_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace249_2_0.value, &replace249_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace249_0.value, &replace249_1.value, &replace249_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search106, &replace106.value, 0 },
   { &search248, &replace248.value, 0 },
   { &search249, &replace249.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search15_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search15_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search15_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search15 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search15_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace15_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace15 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace15_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search16_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search16_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search16_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search16 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search16_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace16_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace16 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace16_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search188_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search188_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search188_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search188 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search188_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace188_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace188 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace188_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search206_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search206_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search206_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search206_0_0.value, &search206_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search206_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace206_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace206_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search15, &replace15.value, 0 },
   { &search16, &replace16.value, 0 },
   { &search188, &replace188.value, 0 },
   { &search206, &replace206.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search230_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_4x8,
   { &search230_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace230_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace230_0_0_0_0.value, &replace230_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace230_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace230_0_0_1_0.value, &replace230_0_0_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace230_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace230_0_0_2_0.value, &replace230_0_0_2_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace230_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace230_0_0_3_0.value, &replace230_0_0_3_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace230_0_0_0.value, &replace230_0_0_1.value, &replace230_0_0_2.value, &replace230_0_0_3.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace230_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace230_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace230_0.value, &replace230_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_unorm_4x8_xforms[] = {
   { &search230, &replace230.value, 34 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search78_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search78_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search78 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search78_0.value, &search78_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace78 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search84_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search84_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search84_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search84_0_0.value, &search84_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search84_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search84 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmin,
   { &search84_0.value, &search84_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace84_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace84_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search88_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search88_0_0_0_0.value, &search88_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search88_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search88_0_0_0.value, &search88_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search88_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search88_0_0.value, &search88_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search88_0.value, &search88_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace88_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace88_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace88_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace88_0_0.value, &replace88_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace88_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace88_0.value, &replace88_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search92_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search92_0_0.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search92_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_zero_to_one),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search92_0.value, &search92_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace92_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace92_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace92_0_0.value, &replace92_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace92_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search78, &replace78.value, 0 },
   { &search84, &replace84.value, 9 },
   { &search88, &replace88.value, 0 },
   { &search92, &replace92.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search118_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search118_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search118 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search118_0.value, &search118_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace118 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search168_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search168_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search168_0.value, &search168_1.value },
   (is_not_used_by_if),
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace168_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace168_0.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search169_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search169_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search169 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search169_0.value, &search169_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace169 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search256_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search256_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search256_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search256_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search256_0_0.value, &search256_0_1.value, &search256_0_2.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search256_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search256 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search256_0.value, &search256_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace256_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace256_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace256_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace256_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace256_1_0.value, &replace256_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace256_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace256_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace256_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace256_2_0.value, &replace256_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace256 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace256_0.value, &replace256_1.value, &replace256_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search257_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search257_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search257_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search257_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search257_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search257_1_0.value, &search257_1_1.value, &search257_1_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search257 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search257_0.value, &search257_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace257_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace257_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace257_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace257_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace257_1_0.value, &replace257_1_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace257_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace257_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace257_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace257_2_0.value, &replace257_2_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace257 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace257_0.value, &replace257_1.value, &replace257_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search118, &replace118.value, 0 },
   { &search168, &replace168.value, 0 },
   { &search169, &replace169.value, 0 },
   { &search256, &replace256.value, 0 },
   { &search257, &replace257.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search2_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_udiv,
   { &search2_0.value, &search2_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search6_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search6_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search6 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_udiv,
   { &search6_0.value, &search6_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace6_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace6_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace6_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace6_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace6 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace6_0.value, &replace6_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_udiv_xforms[] = {
   { &search2, &replace2.value, 0 },
   { &search6, &replace6.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search221_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search221_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &search221_0.value, &search221_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace221_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace221_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace221_0_1_0_0.value, &replace221_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace221_0_1_0.value, &replace221_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace221_0_0.value, &replace221_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace221_0.value, &replace221_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_i8_xforms[] = {
   { &search221, &replace221.value, 27 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search216_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search216_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search216 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_uadd_carry,
   { &search216_0.value, &search216_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace216_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace216_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace216_0_0_0.value, &replace216_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace216_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace216_0_0.value, &replace216_0_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace216_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_uadd_carry_xforms[] = {
   { &search216, &replace216.value, 23 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search219_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search219_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search219_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibitfield_extract,
   { &search219_0.value, &search219_1.value, &search219_2.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace219_0_0.value, &replace219_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibfe,
   { &replace219_2_0.value, &replace219_2_1.value, &replace219_2_2.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace219_0.value, &replace219_1.value, &replace219_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ibitfield_extract_xforms[] = {
   { &search219, &replace219.value, 26 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search233_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search233_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search233 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ldexp,
   { &search233_0.value, &search233_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_0_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace233_0_1_0_0_0_0_0.value, &replace233_0_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace233_0_1_0_0_0_0.value, &replace233_0_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace233_0_1_0_0_0.value, &replace233_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace233_0_1_0_0.value, &replace233_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace233_0_1_0.value, &replace233_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace233_0_0.value, &replace233_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace233_1_0_0_0_0_0.value, &replace233_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace233_1_0_0_0_0.value, &replace233_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace233_1_0_0_1_0_0_0.value, &replace233_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace233_1_0_0_1_0_0.value, &replace233_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace233_1_0_0_1_0.value, &replace233_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace233_1_0_0_0.value, &replace233_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace233_1_0_0.value, &replace233_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace233_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace233_1_0.value, &replace233_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace233_0.value, &replace233_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search234_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search234_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search234 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_ldexp,
   { &search234_0.value, &search234_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_0_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_0_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace234_0_1_1_0_0_0_0_0.value, &replace234_0_1_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_0_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace234_0_1_1_0_0_0_0.value, &replace234_0_1_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_0_1_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace234_0_1_1_0_0_0.value, &replace234_0_1_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_0_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace234_0_1_1_0_0.value, &replace234_0_1_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_0_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace234_0_1_1_0.value, &replace234_0_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_double_2x32_split,
   { &replace234_0_1_0.value, &replace234_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace234_0_0.value, &replace234_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace234_1_1_0_0_0_0_0.value, &replace234_1_1_0_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace234_1_1_0_0_0_0.value, &replace234_1_1_0_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_1_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace234_1_1_0_0_1_0_0_0.value, &replace234_1_1_0_0_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace234_1_1_0_0_1_0_0.value, &replace234_1_1_0_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace234_1_1_0_0_1_0.value, &replace234_1_1_0_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace234_1_1_0_0_0.value, &replace234_1_1_0_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace234_1_1_0_0.value, &replace234_1_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace234_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace234_1_1_0.value, &replace234_1_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_double_2x32_split,
   { &replace234_1_0.value, &replace234_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace234_0.value, &replace234_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ldexp_xforms[] = {
   { &search233, &replace233.value, 0 },
   { &search234, &replace234.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search223_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search223_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &search223_0.value, &search223_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace223_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace223_0_1_0_0.value, &replace223_0_1_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace223_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace223_0_1_0.value, &replace223_0_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace223_0_0.value, &replace223_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace223_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace223_0.value, &replace223_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_i16_xforms[] = {
   { &search223, &replace223.value, 28 },
};

static bool
nir_opt_algebraic_block(nir_block *block, const bool *condition_flags,
                   void *mem_ctx)
{
   bool progress = false;

   nir_foreach_instr_reverse_safe(instr, block) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!alu->dest.dest.is_ssa)
         continue;

      switch (alu->op) {
      case nir_op_iand:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iand_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iand_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_i2b:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_i2b_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_i2b_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ixor:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ixor_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ixor_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_seq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_seq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_seq_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ilt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ilt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ilt_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_umod:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umod_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umod_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_imul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imul_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_uge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_uge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_uge_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ineg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ineg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ineg_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fmul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmul_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ubitfield_extract:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ubitfield_extract_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ubitfield_extract_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ffma:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ffma_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ffma_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_umin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umin_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_umax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umax_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_bcsel:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_bcsel_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_bcsel_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_sge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_sge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_sge_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fsqrt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fsqrt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fsqrt_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_iadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iadd_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_pack_unorm_2x16:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_pack_unorm_2x16_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_pack_unorm_2x16_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_pack_unorm_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_pack_unorm_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_pack_unorm_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_pack_snorm_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_pack_snorm_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_pack_snorm_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fand:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fand_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fand_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fabs:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fabs_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fabs_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_imod:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imod_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imod_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ieq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ieq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ieq_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_imin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imin_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_frsq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_frsq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_frsq_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_usadd_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_usadd_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_usadd_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_idiv:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_idiv_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_idiv_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_extract_u8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_extract_u8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_extract_u8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_pack_snorm_2x16:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_pack_snorm_2x16_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_pack_snorm_2x16_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fpow:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fpow_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fpow_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ige:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ige_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ige_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fdiv:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fdiv_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fdiv_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ffract:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ffract_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ffract_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fadd_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ishl:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ishl_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ishl_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_usub_borrow:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_usub_borrow_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_usub_borrow_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_flog2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flog2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flog2_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_inot:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_inot_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_inot_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_sne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_sne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_sne_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fmod:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmod_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmod_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_f2u:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_f2u_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_f2u_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fcsel:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fcsel_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fcsel_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_isub:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_isub_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_isub_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fmax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmax_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_umul_unorm_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umul_unorm_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umul_unorm_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_bitfield_insert:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_bitfield_insert_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_bitfield_insert_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_feq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_feq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_feq_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_flrp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flrp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flrp_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ior:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ior_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ior_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ussub_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ussub_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ussub_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_unpack_snorm_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_unpack_snorm_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_unpack_snorm_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_frem:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_frem_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_frem_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_unpack_unorm_2x16:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_unpack_unorm_2x16_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_unpack_unorm_2x16_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_unpack_snorm_2x16:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_unpack_snorm_2x16_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_unpack_snorm_2x16_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_imax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imax_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fsat:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fsat_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fsat_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_extract_u16:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_extract_u16_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_extract_u16_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fge_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_frcp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_frcp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_frcp_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fxor:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fxor_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fxor_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ushr:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ushr_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ushr_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fexp2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fexp2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fexp2_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ishr:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ishr_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ishr_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_slt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_slt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_slt_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_f2i:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_f2i_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_f2i_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_flt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flt_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ult:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ult_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ult_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fsub:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fsub_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fsub_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fneg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fneg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fneg_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fne_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_iabs:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iabs_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iabs_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_unpack_unorm_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_unpack_unorm_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_unpack_unorm_4x8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fmin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmin_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ine:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ine_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ine_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_udiv:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_udiv_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_udiv_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_extract_i8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_extract_i8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_extract_i8_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_uadd_carry:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_uadd_carry_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_uadd_carry_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ibitfield_extract:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ibitfield_extract_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ibitfield_extract_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_ldexp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ldexp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ldexp_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_extract_i16:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_extract_i16_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_extract_i16_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      default:
         break;
      }
   }

   return progress;
}

static bool
nir_opt_algebraic_impl(nir_function_impl *impl, const bool *condition_flags)
{
   void *mem_ctx = ralloc_parent(impl);
   bool progress = false;

   nir_foreach_block_reverse(block, impl) {
      progress |= nir_opt_algebraic_block(block, condition_flags, mem_ctx);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}


bool
nir_opt_algebraic(nir_shader *shader)
{
   bool progress = false;
   bool condition_flags[37];
   const nir_shader_compiler_options *options = shader->options;
   (void) options;

   condition_flags[0] = true;
   condition_flags[1] = options->lower_idiv;
   condition_flags[2] = options->lower_flrp32;
   condition_flags[3] = options->lower_flrp64;
   condition_flags[4] = options->lower_ffract;
   condition_flags[5] = !options->lower_flrp32;
   condition_flags[6] = !options->lower_flrp64;
   condition_flags[7] = options->lower_ffma;
   condition_flags[8] = options->fuse_ffma;
   condition_flags[9] = !options->lower_fsat;
   condition_flags[10] = options->lower_fsat;
   condition_flags[11] = options->lower_scmp;
   condition_flags[12] = options->lower_fpow;
   condition_flags[13] = !options->lower_fpow;
   condition_flags[14] = options->lower_fdiv;
   condition_flags[15] = options->lower_fsqrt;
   condition_flags[16] = !options->lower_fsqrt;
   condition_flags[17] = !options->lower_extract_byte;
   condition_flags[18] = !options->lower_extract_word;
   condition_flags[19] = options->lower_sub;
   condition_flags[20] = options->lower_negate;
   condition_flags[21] = options->lower_fmod32;
   condition_flags[22] = options->lower_fmod64;
   condition_flags[23] = options->lower_uadd_carry;
   condition_flags[24] = options->lower_usub_borrow;
   condition_flags[25] = options->lower_bitfield_insert;
   condition_flags[26] = options->lower_bitfield_extract;
   condition_flags[27] = options->lower_extract_byte;
   condition_flags[28] = options->lower_extract_word;
   condition_flags[29] = options->lower_pack_unorm_2x16;
   condition_flags[30] = options->lower_pack_unorm_4x8;
   condition_flags[31] = options->lower_pack_snorm_2x16;
   condition_flags[32] = options->lower_pack_snorm_4x8;
   condition_flags[33] = options->lower_unpack_unorm_2x16;
   condition_flags[34] = options->lower_unpack_unorm_4x8;
   condition_flags[35] = options->lower_unpack_snorm_2x16;
   condition_flags[36] = options->lower_unpack_snorm_4x8;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_algebraic_impl(function->impl, condition_flags);
   }

   return progress;
}


#include "nir.h"
#include "nir_search.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search270_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search270_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search270_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search270 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search270_0.value },
   (is_used_more_than_once),
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace270_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace270_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace270_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace270 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace270_0.value, &replace270_1.value, &replace270_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_b2f_xforms[] = {
   { &search270, &replace270.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search268_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search268_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search268 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search268_0.value, &search268_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace268_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace268_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace268 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated4,
   { &replace268_0.value, &replace268_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot4_xforms[] = {
   { &search268, &replace268.value, 37 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search262_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search262_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search262_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search262_0_0.value, &search262_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search262_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search262 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search262_0.value, &search262_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace262_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace262_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace262_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace262_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace262 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace262_0.value, &replace262_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_flt_xforms[] = {
   { &search262, &replace262.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search266_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search266_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search266 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot2,
   { &search266_0.value, &search266_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace266_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace266_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace266 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated2,
   { &replace266_0.value, &replace266_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot2_xforms[] = {
   { &search266, &replace266.value, 37 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search267_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search267_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search267 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search267_0.value, &search267_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace267_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace267_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace267 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated3,
   { &replace267_0.value, &replace267_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot3_xforms[] = {
   { &search267, &replace267.value, 37 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search271_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search271_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search271_0_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search271_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search271_0_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search271 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search271_0.value },
   (is_used_more_than_once),
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace271_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace271_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace271_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace271 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace271_0.value, &replace271_1.value, &replace271_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fneg_xforms[] = {
   { &search271, &replace271.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search273_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search273_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search273_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search273_0_0.value, &search273_0_1.value },
   (is_used_once),
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search273_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search273_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search273_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search273_1_0.value, &search273_1_1.value },
   (is_used_once),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search273 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search273_0.value, &search273_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace273_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace273_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace273_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace273_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace273_1_0.value, &replace273_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace273 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace273_0.value, &replace273_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fmax_xforms[] = {
   { &search273, &replace273.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search265_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search265_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search265_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search265_0_0.value, &search265_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search265_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search265 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fne,
   { &search265_0.value, &search265_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace265_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace265_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace265_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace265_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace265 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace265_0.value, &replace265_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fne_xforms[] = {
   { &search265, &replace265.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search272_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search272_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search272_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search272_0_0.value, &search272_0_1.value },
   (is_used_once),
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search272_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search272_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search272_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search272_1_0.value, &search272_1_1.value },
   (is_used_once),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search272 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search272_0.value, &search272_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace272_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace272_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace272_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace272_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace272_1_0.value, &replace272_1_1.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace272 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace272_0.value, &replace272_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fmin_xforms[] = {
   { &search272, &replace272.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search264_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search264_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search264_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search264_0_0.value, &search264_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search264_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search264 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_feq,
   { &search264_0.value, &search264_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace264_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace264_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace264_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace264_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace264 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace264_0.value, &replace264_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_feq_xforms[] = {
   { &search264, &replace264.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search263_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search263_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search263_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search263_0_0.value, &search263_0_1.value },
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search263_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search263 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search263_0.value, &search263_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace263_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace263_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace263_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace263_1_0.value },
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace263 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace263_0.value, &replace263_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fge_xforms[] = {
   { &search263, &replace263.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search269_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search269_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search269 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph,
   { &search269_0.value, &search269_1.value },
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace269_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace269_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace269 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph_replicated,
   { &replace269_0.value, &replace269_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdph_xforms[] = {
   { &search269, &replace269.value, 37 },
};

static bool
nir_opt_algebraic_late_block(nir_block *block, const bool *condition_flags,
                   void *mem_ctx)
{
   bool progress = false;

   nir_foreach_instr_reverse_safe(instr, block) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!alu->dest.dest.is_ssa)
         continue;

      switch (alu->op) {
      case nir_op_b2f:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_b2f_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_b2f_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fdot4:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdot4_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdot4_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_flt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_flt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_flt_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fdot2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdot2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdot2_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fdot3:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdot3_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdot3_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fneg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fneg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fneg_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fmax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fmax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fmax_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fne_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fmin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fmin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fmin_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_feq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_feq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_feq_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fge_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fdph:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdph_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdph_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      default:
         break;
      }
   }

   return progress;
}

static bool
nir_opt_algebraic_late_impl(nir_function_impl *impl, const bool *condition_flags)
{
   void *mem_ctx = ralloc_parent(impl);
   bool progress = false;

   nir_foreach_block_reverse(block, impl) {
      progress |= nir_opt_algebraic_late_block(block, condition_flags, mem_ctx);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}


bool
nir_opt_algebraic_late(nir_shader *shader)
{
   bool progress = false;
   bool condition_flags[38];
   const nir_shader_compiler_options *options = shader->options;
   (void) options;

   condition_flags[0] = true;
   condition_flags[1] = options->lower_idiv;
   condition_flags[2] = options->lower_flrp32;
   condition_flags[3] = options->lower_flrp64;
   condition_flags[4] = options->lower_ffract;
   condition_flags[5] = !options->lower_flrp32;
   condition_flags[6] = !options->lower_flrp64;
   condition_flags[7] = options->lower_ffma;
   condition_flags[8] = options->fuse_ffma;
   condition_flags[9] = !options->lower_fsat;
   condition_flags[10] = options->lower_fsat;
   condition_flags[11] = options->lower_scmp;
   condition_flags[12] = options->lower_fpow;
   condition_flags[13] = !options->lower_fpow;
   condition_flags[14] = options->lower_fdiv;
   condition_flags[15] = options->lower_fsqrt;
   condition_flags[16] = !options->lower_fsqrt;
   condition_flags[17] = !options->lower_extract_byte;
   condition_flags[18] = !options->lower_extract_word;
   condition_flags[19] = options->lower_sub;
   condition_flags[20] = options->lower_negate;
   condition_flags[21] = options->lower_fmod32;
   condition_flags[22] = options->lower_fmod64;
   condition_flags[23] = options->lower_uadd_carry;
   condition_flags[24] = options->lower_usub_borrow;
   condition_flags[25] = options->lower_bitfield_insert;
   condition_flags[26] = options->lower_bitfield_extract;
   condition_flags[27] = options->lower_extract_byte;
   condition_flags[28] = options->lower_extract_word;
   condition_flags[29] = options->lower_pack_unorm_2x16;
   condition_flags[30] = options->lower_pack_unorm_4x8;
   condition_flags[31] = options->lower_pack_snorm_2x16;
   condition_flags[32] = options->lower_pack_snorm_4x8;
   condition_flags[33] = options->lower_unpack_unorm_2x16;
   condition_flags[34] = options->lower_unpack_unorm_4x8;
   condition_flags[35] = options->lower_unpack_snorm_2x16;
   condition_flags[36] = options->lower_unpack_snorm_4x8;
   condition_flags[37] = options->fdot_replicates;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_algebraic_late_impl(function->impl, condition_flags);
   }

   return progress;
}

