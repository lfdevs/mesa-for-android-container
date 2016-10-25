
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
static const nir_search_variable search103_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search103_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search103_0.value, &search103_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace103_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace103_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search113_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search113_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search113_0.value, &search113_1.value },
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
static const nir_search_variable search114_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search114_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search114_0.value, &search114_1.value },
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
static const nir_search_variable search115_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search115_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search115_0.value, &search115_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace115 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search123_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search123_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search123_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search123_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search123_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search123_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search123 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search123_0.value, &search123_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace123_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace123_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace123_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace123_0_0.value, &replace123_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace123 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace123_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search131_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search131_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search131_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search131_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search131_1_0.value, &search131_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search131_0.value, &search131_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace131_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace131_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace131_0.value, &replace131_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search132_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search132_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search132_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search132_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search132_1_0.value, &search132_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search132_0.value, &search132_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace132_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace132_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace132_0.value, &replace132_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search177_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search177_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search177_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search177_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search177_1_0.value, &search177_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search177_0.value, &search177_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace177_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace177_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace177_0.value, &replace177_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search178_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search178_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search178_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search178_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search178_1_0.value, &search178_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search178_0.value, &search178_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace178_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace178_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace178_0.value, &replace178_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search179_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search179_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search179_0.value, &search179_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace179_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace179_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace179_0.value, &replace179_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search181_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search181_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search181_0.value, &search181_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace181_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace181_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace181_0.value, &replace181_1.value },
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search103, &replace103.value, 0 },
   { &search113, &replace113.value, 0 },
   { &search114, &replace114.value, 0 },
   { &search115, &replace115.value, 0 },
   { &search123, &replace123.value, 0 },
   { &search131, &replace131.value, 0 },
   { &search132, &replace132.value, 0 },
   { &search177, &replace177.value, 17 },
   { &search178, &replace178.value, 17 },
   { &search179, &replace179.value, 17 },
   { &search181, &replace181.value, 18 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search169_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search169_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search169_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search169 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search169_0.value },
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
static const nir_search_variable search172_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search172_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search172_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search172 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search172_0.value },
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
   nir_op_i2b,
   { &replace172_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search173_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search173_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search173_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search173 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search173_0.value },
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
   nir_op_i2b,
   { &replace173_0.value },
};

static const struct transform nir_opt_algebraic_i2b_xforms[] = {
   { &search169, &replace169.value, 0 },
   { &search172, &replace172.value, 0 },
   { &search173, &replace173.value, 0 },
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
   nir_op_ixor,
   { &search120_0.value, &search120_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace120 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search121_0.value, &search121_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace121 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search120, &replace120.value, 0 },
   { &search121, &replace121.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search96_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search96_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search96_0.value, &search96_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace96_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace96_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace96_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace96_0_0.value, &replace96_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace96_0.value },
};

static const struct transform nir_opt_algebraic_seq_xforms[] = {
   { &search96, &replace96.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search106_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_op_ilt,
   { &search106_0.value, &search106_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace106 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search231_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search231_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search231_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search231_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search231_0_0.value, &search231_0_1.value, &search231_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search231_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search231_0.value, &search231_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace231_1_0.value, &replace231_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace231_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace231_2_0.value, &replace231_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace231_0.value, &replace231_1.value, &replace231_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search232_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search232_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search232_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search232_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search232_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search232_1_0.value, &search232_1_1.value, &search232_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search232_0.value, &search232_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace232_1_0.value, &replace232_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace232_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace232_2_0.value, &replace232_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace232_0.value, &replace232_1.value, &replace232_2.value },
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search106, &replace106.value, 0 },
   { &search231, &replace231.value, 0 },
   { &search232, &replace232.value, 0 },
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
static const nir_search_variable search5_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search5 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umod,
   { &search5_0.value, &search5_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace5_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace5_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace5_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace5_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace5_1_0.value, &replace5_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace5 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace5_0.value, &replace5_1.value },
};

static const struct transform nir_opt_algebraic_umod_xforms[] = {
   { &search5, &replace5.value, 0 },
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
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace0_0.value, &replace0_1.value },
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
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace1_0_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace1_0_0.value, &replace1_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace1_0.value },
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
static const nir_search_constant search26_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search26 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search26_0.value, &search26_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace26 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search30 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search30_0.value, &search30_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace30 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
   nir_op_imul,
   { &search32_0.value, &search32_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace32_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace32 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace32_0.value },
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
static const nir_search_expression search100_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search100_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search100_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search100_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search100_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search100_0.value, &search100_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace100_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace100_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace100_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace100_0_0.value, &replace100_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace100_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search195_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search195_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search195_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search195_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search195_0.value, &search195_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace195_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace195_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace195_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace195_0_0.value, &replace195_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace195_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search197_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search197_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search197_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search197_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search197_1_0.value, &search197_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search197_0.value, &search197_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace197_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace197_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace197_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace197_0_0.value, &replace197_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace197_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace197_0.value, &replace197_1.value },
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search0, &replace0.value, 0 },
   { &search1, &replace1.value, 0 },
   { &search26, &replace26.value, 0 },
   { &search30, &replace30.value, 0 },
   { &search32, &replace32.value, 0 },
   { &search100, &replace100.value, 0 },
   { &search195, &replace195.value, 0 },
   { &search197, &replace197.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search111_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search111_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search111_0.value, &search111_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace111 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
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
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search241_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search241_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search241_0_0.value, &search241_0_1.value, &search241_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search241_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search241_0.value, &search241_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace241_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace241_1_0.value, &replace241_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace241_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace241_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace241_2_0.value, &replace241_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace241_0.value, &replace241_1.value, &replace241_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search242_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search242_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search242_1_0.value, &search242_1_1.value, &search242_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search242_0.value, &search242_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace242_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace242_1_0.value, &replace242_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace242_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace242_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace242_2_0.value, &replace242_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace242_0.value, &replace242_1.value, &replace242_2.value },
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search111, &replace111.value, 0 },
   { &search241, &replace241.value, 0 },
   { &search242, &replace242.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search7_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search7_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search7_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search7 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search7_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace7 = {
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
static const nir_search_expression search189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search189_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace189_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace189_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace189_0.value, &replace189_1.value },
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search7, &replace7.value, 0 },
   { &search189, &replace189.value, 20 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search25_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search25_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search25 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search25_0.value, &search25_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace25 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
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
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search29 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search29_0.value, &search29_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace29 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search31 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search31_0.value, &search31_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace31_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace31 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace31_0.value },
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
static const nir_search_expression search101_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search101_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search101_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search101_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search101_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search101_0.value, &search101_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace101_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace101_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace101_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace101_0_0.value, &replace101_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace101_0.value },
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
static const nir_search_expression search151_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search151_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search151_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search151_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search151_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search151 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search151_0.value, &search151_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace151_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace151_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace151_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace151_0_0.value, &replace151_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace151 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace151_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search194_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search194_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search194_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search194_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search194_0.value, &search194_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace194_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace194_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace194_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace194_0_0.value, &replace194_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace194_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search196_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search196_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search196_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search196_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search196_1_0.value, &search196_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search196 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search196_0.value, &search196_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace196_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace196_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace196_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace196_0_0.value, &replace196_0_1.value },
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
   nir_op_fmul,
   { &replace196_0.value, &replace196_1.value },
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search25, &replace25.value, 0 },
   { &search29, &replace29.value, 0 },
   { &search31, &replace31.value, 0 },
   { &search101, &replace101.value, 0 },
   { &search151, &replace151.value, 0 },
   { &search194, &replace194.value, 0 },
   { &search196, &replace196.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search207_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search207_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search207_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubitfield_extract,
   { &search207_0.value, &search207_1.value, &search207_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace207_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace207_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace207_0_0.value, &replace207_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace207_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace207_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubfe,
   { &replace207_2_0.value, &replace207_2_1.value, &replace207_2_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace207_0.value, &replace207_1.value, &replace207_2.value },
};

static const struct transform nir_opt_algebraic_ubitfield_extract_xforms[] = {
   { &search207, &replace207.value, 26 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search33_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search33_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search33_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search33 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search33_0.value, &search33_1.value, &search33_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace33 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
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
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search34_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search34 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search34_0.value, &search34_1.value, &search34_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace34 = {
   { nir_search_value_variable, 0 },
   1, /* b */
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
static const nir_search_variable search35_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search35_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search35 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search35_0.value, &search35_1.value, &search35_2.value },
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
static const nir_search_variable replace35_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace35 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace35_0.value, &replace35_1.value },
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
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search36_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search36_0.value, &search36_1.value, &search36_2.value },
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
static const nir_search_variable replace36_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace36_0.value, &replace36_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search37_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
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
   false,
   nir_op_ffma,
   { &search37_0.value, &search37_1.value, &search37_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace37_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace37_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace37 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace37_0.value, &replace37_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search52_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search52 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search52_0.value, &search52_1.value, &search52_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace52_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace52_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace52_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace52_0_0.value, &replace52_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace52_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace52 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace52_0.value, &replace52_1.value },
};

static const struct transform nir_opt_algebraic_ffma_xforms[] = {
   { &search33, &replace33.value, 0 },
   { &search34, &replace34.value, 0 },
   { &search35, &replace35.value, 0 },
   { &search36, &replace36.value, 0 },
   { &search37, &replace37.value, 0 },
   { &search52, &replace52.value, 7 },
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
static const nir_search_variable search76_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search76_0.value, &search76_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace76 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search84_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search84_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search84_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search84_0_0_0_0.value, &search84_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search84_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search84_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search84_0_0_0.value, &search84_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search84_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search84_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search84_0_0.value, &search84_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search84_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search84_0.value, &search84_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace84_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace84_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace84_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace84_0_0.value, &replace84_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace84_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace84_0.value, &replace84_1.value },
};

static const struct transform nir_opt_algebraic_umin_xforms[] = {
   { &search76, &replace76.value, 0 },
   { &search84, &replace84.value, 0 },
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
static const nir_search_variable search77_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search77_0.value, &search77_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace77 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_umax_xforms[] = {
   { &search77, &replace77.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search67_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search67_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search67_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search67_0_0.value, &search67_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search67_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search67_2 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search67 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search67_0.value, &search67_1.value, &search67_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace67_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace67_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace67 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace67_0.value, &replace67_1.value },
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
   nir_op_flt,
   { &search68_0_0.value, &search68_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search68_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search68_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search68 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search68_0.value, &search68_1.value, &search68_2.value },
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
   nir_op_fmax,
   { &replace68_0.value, &replace68_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search69_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search69_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search69_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search69_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search69_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search69_0.value, &search69_1.value, &search69_2.value },
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
static const nir_search_variable replace69_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace69_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace69_0.value, &replace69_1.value, &replace69_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search70_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search70_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search70_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search70_1_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search70_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search70_1_0.value, &search70_1_1.value, &search70_1_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search70_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search70_0.value, &search70_1.value, &search70_2.value },
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
static const nir_search_variable replace70_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace70_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace70_0.value, &replace70_1.value, &replace70_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search71_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search71_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search71_2 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_bool32,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search71_0.value, &search71_1.value, &search71_2.value },
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
static const nir_search_variable replace71_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace71_0.value, &replace71_1.value },
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
static const nir_search_constant search162_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search162_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search162_0.value, &search162_1.value, &search162_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace162 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search163_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search163_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search163_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search163 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search163_0.value, &search163_1.value, &search163_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace163_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace163 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace163_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search164_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search164_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search164_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search164_0.value, &search164_1.value, &search164_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace164 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search165_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search165_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search165_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search165_0.value, &search165_1.value, &search165_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace165 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search166_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search166_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search166_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search166_0.value, &search166_1.value, &search166_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace166_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace166_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace166_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace166_0_0.value, &replace166_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace166_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace166_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace166_0.value, &replace166_1.value, &replace166_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search167_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search167_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search167_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search167_0.value, &search167_1.value, &search167_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace167 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_bcsel_xforms[] = {
   { &search67, &replace67.value, 0 },
   { &search68, &replace68.value, 0 },
   { &search69, &replace69.value, 0 },
   { &search70, &replace70.value, 0 },
   { &search71, &replace71.value, 0 },
   { &search162, &replace162.value, 0 },
   { &search163, &replace163.value, 0 },
   { &search164, &replace164.value, 0 },
   { &search165, &replace165.value, 0 },
   { &search166, &replace166.value, 0 },
   { &search167, &replace167.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search95_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search95_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search95_0.value, &search95_1.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace95_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace95_0_0.value, &replace95_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace95_0.value },
};

static const struct transform nir_opt_algebraic_sge_xforms[] = {
   { &search95, &replace95.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search144_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search144_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search144_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search144 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsqrt,
   { &search144_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace144_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace144_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
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
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace144 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace144_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search156_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search156_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace156_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace156_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace156_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace156_0.value },
};

static const struct transform nir_opt_algebraic_fsqrt_xforms[] = {
   { &search144, &replace144.value, 0 },
   { &search156, &replace156.value, 15 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search14_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search14_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search14 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search14_0.value, &search14_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace14 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search18_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search18_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search18_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search18_0_0.value, &search18_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search18_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search18_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search18_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search18_1_0.value, &search18_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search18 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search18_0.value, &search18_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace18_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace18_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace18_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace18_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace18_1_0.value, &replace18_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace18 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace18_0.value, &replace18_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search20_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search20_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search20_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search20_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search20 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search20_0.value, &search20_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace20 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
static const nir_search_expression search21_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search21_0_0.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search21_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search21_1_0.value, &search21_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search21 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search21_0.value, &search21_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace21 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search22_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search22_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search22_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search22_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search22_1_0.value, &search22_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search22 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search22_0.value, &search22_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace22 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search191_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search191_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search191_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search191_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search191_1_0.value, &search191_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search191_0.value, &search191_1.value },
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
static const nir_search_variable replace191_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace191_0.value, &replace191_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search199_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search199_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search199_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search199_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search199_1_0.value, &search199_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search199_0.value, &search199_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace199_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace199_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace199_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace199_0_0.value, &replace199_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace199_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace199_0.value, &replace199_1.value },
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search14, &replace14.value, 0 },
   { &search18, &replace18.value, 0 },
   { &search20, &replace20.value, 0 },
   { &search21, &replace21.value, 0 },
   { &search22, &replace22.value, 0 },
   { &search191, &replace191.value, 0 },
   { &search199, &replace199.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search212_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_2x16,
   { &search212_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace212_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace212_0_0_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace212_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace212_0_0_0_0.value, &replace212_0_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace212_0_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace212_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace212_0.value },
};

static const struct transform nir_opt_algebraic_pack_unorm_2x16_xforms[] = {
   { &search212, &replace212.value, 29 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search213_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_4x8,
   { &search213_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace213_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace213_0_0_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace213_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace213_0_0_0_0.value, &replace213_0_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace213_0_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace213_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace213_0.value },
};

static const struct transform nir_opt_algebraic_pack_unorm_4x8_xforms[] = {
   { &search213, &replace213.value, 30 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search215_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_4x8,
   { &search215_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace215_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace215_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace215_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace215_0_0_0_0_1_0.value, &replace215_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace215_0_0_0_0_0.value, &replace215_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace215_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace215_0_0_0_0.value, &replace215_0_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace215_0_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace215_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace215_0.value },
};

static const struct transform nir_opt_algebraic_pack_snorm_4x8_xforms[] = {
   { &search215, &replace215.value, 32 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search112_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search112_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fand,
   { &search112_0.value, &search112_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace112 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search112, &replace112.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search8_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search8_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search8_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search8 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search8_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace8_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace8 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace8_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search9_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search9_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search9_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search9 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search9_0.value },
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
static const nir_search_expression replace9 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace9_0.value },
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
   nir_op_u2f,
   { &search10_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search10 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search10_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace10_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace10 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace10_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search90_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_op_slt,
   { &search90_0_0.value, &search90_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search90_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace90_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace90_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &replace90_0.value, &replace90_1.value },
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
static const nir_search_variable search91_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search91_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search91_0_0.value, &search91_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search91_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace91_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace91_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &replace91_0.value, &replace91_1.value },
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
static const nir_search_variable search92_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search92_0_0.value, &search92_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search92_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace92_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace92_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &replace92_0.value, &replace92_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search93_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search93_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search93_0_0.value, &search93_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search93_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace93_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace93_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &replace93_0.value, &replace93_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search174_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search174_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search174_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search174 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search174_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace174_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace174 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace174_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search192_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search192_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search192_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search192_0_0.value, &search192_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search192_0.value },
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
static const nir_search_expression replace192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace192_0.value },
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search8, &replace8.value, 0 },
   { &search9, &replace9.value, 0 },
   { &search10, &replace10.value, 0 },
   { &search90, &replace90.value, 0 },
   { &search91, &replace91.value, 0 },
   { &search92, &replace92.value, 0 },
   { &search93, &replace93.value, 0 },
   { &search174, &replace174.value, 0 },
   { &search192, &replace192.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search108_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search108_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search108 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search108_0.value, &search108_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace108 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search158_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search158_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search158 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search158_0.value, &search158_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace158 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search161_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search161_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search161_0.value, &search161_1.value },
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
   nir_op_inot,
   { &replace161_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search235_0_0.value, &search235_0_1.value, &search235_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search235_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search235_0.value, &search235_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace235_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace235_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace235_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace235_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace235_1_0.value, &replace235_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace235_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace235_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace235_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace235_2_0.value, &replace235_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace235_0.value, &replace235_1.value, &replace235_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search236_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search236_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search236_1_0.value, &search236_1_1.value, &search236_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search236_0.value, &search236_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace236_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace236_1_0.value, &replace236_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace236_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace236_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace236_2_0.value, &replace236_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace236_0.value, &replace236_1.value, &replace236_2.value },
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search108, &replace108.value, 0 },
   { &search158, &replace158.value, 0 },
   { &search161, &replace161.value, 0 },
   { &search235, &replace235.value, 0 },
   { &search236, &replace236.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search74_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search74_1 = {
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
   nir_op_imin,
   { &search74_0.value, &search74_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace74 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search83_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search83_0_0_0_0.value, &search83_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search83_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search83_0_0_0.value, &search83_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search83_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search83_0_0.value, &search83_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search83_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search83_0.value, &search83_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace83_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace83_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace83_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace83_0_0.value, &replace83_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace83_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace83_0.value, &replace83_1.value },
};

static const struct transform nir_opt_algebraic_imin_xforms[] = {
   { &search74, &replace74.value, 0 },
   { &search83, &replace83.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search146_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search146_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search146 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frsq,
   { &search146_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace146_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace146_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace146_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace146_0_0.value, &replace146_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace146 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace146_0.value },
};

static const struct transform nir_opt_algebraic_frsq_xforms[] = {
   { &search146, &replace146.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search15_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search15_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search15 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search15_0.value, &search15_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace15 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search16_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search16_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search16 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search16_0.value, &search16_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace16 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};

static const struct transform nir_opt_algebraic_usadd_4x8_xforms[] = {
   { &search15, &replace15.value, 0 },
   { &search16, &replace16.value, 0 },
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
static const nir_search_variable search3_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search3_0.value, &search3_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace3_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace3_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isign,
   { &replace3_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace3_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace3_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace3_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace3_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace3_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace3_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace3_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace3_1_0.value, &replace3_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace3_0.value, &replace3_1.value },
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
static const nir_search_variable search4_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_neg_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search4 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search4_0.value, &search4_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace4_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isign,
   { &replace4_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace4_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace4_0_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace4_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace4_0_1_1_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace4_0_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace4_0_1_0.value, &replace4_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace4_0_0.value, &replace4_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace4 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace4_0.value },
};

static const struct transform nir_opt_algebraic_idiv_xforms[] = {
   { &search3, &replace3.value, 1 },
   { &search4, &replace4.value, 1 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search85_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search85_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search85_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search85_0_0_0.value, &search85_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search85_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search85_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search85_0_0.value, &search85_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search85_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search85_0.value, &search85_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace85_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace85_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace85_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace85_0_0.value, &replace85_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace85_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace85_0.value, &replace85_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search209_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search209_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search209_0.value, &search209_1.value },
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
static const nir_search_variable replace209_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace209_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace209_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace209_0_1_0.value, &replace209_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace209_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace209_0_0.value, &replace209_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace209_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace209_0.value, &replace209_1.value },
};

static const struct transform nir_opt_algebraic_extract_u8_xforms[] = {
   { &search85, &replace85.value, 0 },
   { &search209, &replace209.value, 27 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search214_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_2x16,
   { &search214_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace214_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace214_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace214_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace214_0_0_0_0_1_0.value, &replace214_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace214_0_0_0_0_0.value, &replace214_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace214_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace214_0_0_0_0.value, &replace214_0_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace214_0_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace214_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace214_0.value },
};

static const struct transform nir_opt_algebraic_pack_snorm_2x16_xforms[] = {
   { &search214, &replace214.value, 31 },
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
static const nir_search_variable search135_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search135_0.value, &search135_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace135_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace135_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace135_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace135_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace135_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace135_0_0.value, &replace135_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace135_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search138_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search138_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search138 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search138_0.value, &search138_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace138 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search139 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search139_0.value, &search139_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace139_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace139_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace139 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace139_0.value, &replace139_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search140_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search140_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4010000000000000 /* 4.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search140 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search140_0.value, &search140_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace140_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace140_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace140_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace140_0_0.value, &replace140_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace140_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace140_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace140_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace140_1_0.value, &replace140_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace140 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace140_0.value, &replace140_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search141_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search141_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search141 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search141_0.value, &search141_1.value },
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
static const nir_search_expression replace141 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace141_0.value },
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
static const nir_search_constant search142_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search142_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search142_0_0.value, &search142_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search142_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search142 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search142_0.value, &search142_1.value },
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
static const nir_search_variable search143_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search143_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search143_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search143_0_0_0.value, &search143_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search143_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search143_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search143_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search143 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search143_0.value, &search143_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace143_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace143 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace143_0.value },
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search135, &replace135.value, 12 },
   { &search138, &replace138.value, 0 },
   { &search139, &replace139.value, 0 },
   { &search140, &replace140.value, 0 },
   { &search141, &replace141.value, 0 },
   { &search142, &replace142.value, 0 },
   { &search143, &replace143.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search107_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_op_ige,
   { &search107_0.value, &search107_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace107 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search233_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search233_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search233_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search233_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search233_0_0.value, &search233_0_1.value, &search233_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search233_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search233_0.value, &search233_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace233_1_0.value, &replace233_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace233_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace233_2_0.value, &replace233_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace233_0.value, &replace233_1.value, &replace233_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search234_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search234_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search234_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search234_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search234_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search234_1_0.value, &search234_1_1.value, &search234_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search234_0.value, &search234_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace234_1_0.value, &replace234_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace234_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace234_2_0.value, &replace234_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace234_0.value, &replace234_1.value, &replace234_2.value },
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search107, &replace107.value, 0 },
   { &search233, &replace233.value, 0 },
   { &search234, &replace234.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search152_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search152_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search152 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fdiv,
   { &search152_0.value, &search152_1.value },
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
   nir_op_frcp,
   { &replace152_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search153_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search153_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &search153_0.value, &search153_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace153_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace153_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace153_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace153_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace153_0.value, &replace153_1.value },
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search152, &replace152.value, 0 },
   { &search153, &replace153.value, 14 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search45_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search45 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffract,
   { &search45_0.value },
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
static const nir_search_variable replace45_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace45_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace45_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace45 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace45_0.value, &replace45_1.value },
};

static const struct transform nir_opt_algebraic_ffract_xforms[] = {
   { &search45, &replace45.value, 4 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search13_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search13_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search13 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search13_0.value, &search13_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace13 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search17_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search17_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search17_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search17_0_0.value, &search17_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search17_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search17_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search17_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search17_1_0.value, &search17_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search17 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search17_0.value, &search17_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace17_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace17_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace17_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace17_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace17_1_0.value, &replace17_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace17 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace17_0.value, &replace17_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search19_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search19_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search19_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search19_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search19 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search19_0.value, &search19_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace19 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
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
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search23_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search23_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search23_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search23_1_0.value, &search23_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search23 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search23_0.value, &search23_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace23 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search24_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search24_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search24_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search24_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search24_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search24_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search24_1_0.value, &search24_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search24 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search24_0.value, &search24_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace24 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search46_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search46_0_1_1_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search46_0_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search46_0_1_0.value, &search46_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search46_0_0.value, &search46_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search46_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search46_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search46_1_0.value, &search46_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search46 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search46_0.value, &search46_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace46_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace46_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
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
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search47_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search47_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search47_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search47_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search47_0_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search47_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search47_0_1_0.value, &search47_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search47_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search47_0_0.value, &search47_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search47_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search47_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search47_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search47_1_0.value, &search47_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search47 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search47_0.value, &search47_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace47_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace47_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace47_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace47 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace47_0.value, &replace47_1.value, &replace47_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search48_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search48_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search48_0_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search48_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search48_0_1_0.value, &search48_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search48_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search48_0_0.value, &search48_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search48_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search48_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search48_1_0.value, &search48_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search48 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search48_0.value, &search48_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace48_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace48 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace48_0.value, &replace48_1.value, &replace48_2.value },
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
static const nir_search_variable search49_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search49_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search49_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search49_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search49_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search49_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search49_1_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search49_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search49_1_1_0.value, &search49_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search49_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search49_1_0.value, &search49_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search49 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search49_0.value, &search49_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace49_2 = {
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
   nir_op_bcsel,
   { &replace49_0.value, &replace49_1.value, &replace49_2.value },
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
static const nir_search_variable search50_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search50_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search50_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search50_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search50_1_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search50_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search50_1_1_0.value, &search50_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search50_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search50_1_0.value, &search50_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search50 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search50_0.value, &search50_1.value },
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
static const nir_search_variable replace50_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace50_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace50 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace50_0.value, &replace50_1.value, &replace50_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search51_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search51_1_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search51_1_1_0.value, &search51_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search51_1_0.value, &search51_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search51 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search51_0.value, &search51_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace51_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
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
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace51 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace51_0.value, &replace51_1.value, &replace51_2.value },
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
static const nir_search_variable search53_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search53_0_0.value, &search53_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search53_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search53 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search53_0.value, &search53_1.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace53_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace53 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &replace53_0.value, &replace53_1.value, &replace53_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search190_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search190_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search190_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search190_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search190_1_0.value, &search190_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search190 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search190_0.value, &search190_1.value },
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
static const nir_search_variable replace190_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace190_0.value, &replace190_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search198_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search198_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search198_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search198_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search198_1_0.value, &search198_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search198 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search198_0.value, &search198_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace198_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace198_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace198_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace198_0_0.value, &replace198_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace198_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace198_0.value, &replace198_1.value },
};

static const struct transform nir_opt_algebraic_fadd_xforms[] = {
   { &search13, &replace13.value, 0 },
   { &search17, &replace17.value, 0 },
   { &search19, &replace19.value, 0 },
   { &search23, &replace23.value, 0 },
   { &search24, &replace24.value, 0 },
   { &search46, &replace46.value, 2 },
   { &search47, &replace47.value, 5 },
   { &search48, &replace48.value, 6 },
   { &search49, &replace49.value, 2 },
   { &search50, &replace50.value, 5 },
   { &search51, &replace51.value, 6 },
   { &search53, &replace53.value, 8 },
   { &search190, &replace190.value, 0 },
   { &search198, &replace198.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search54_0_0_0.value, &search54_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search54_0_0.value, &search54_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search54_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search54 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search54_0.value, &search54_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace54_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace54_0_1_0.value, &replace54_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace54_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace54_0_0.value, &replace54_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace54_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace54_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace54_1_0.value, &replace54_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace54 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace54_0.value, &replace54_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search55_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search55_0_0.value, &search55_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search55_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search55 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search55_0.value, &search55_1.value },
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
static const nir_search_variable replace55_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace55_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace55_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace55_1_0.value, &replace55_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace55 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace55_0.value, &replace55_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search125_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
   nir_op_ishl,
   { &search125_0.value, &search125_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace125 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
   nir_op_ishl,
   { &search126_0.value, &search126_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace126 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search54, &replace54.value, 0 },
   { &search55, &replace55.value, 0 },
   { &search125, &replace125.value, 0 },
   { &search126, &replace126.value, 0 },
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
static const nir_search_variable search204_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search204 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_usub_borrow,
   { &search204_0.value, &search204_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace204_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace204_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace204_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace204_0_0.value, &replace204_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace204_0.value },
};

static const struct transform nir_opt_algebraic_usub_borrow_xforms[] = {
   { &search204, &replace204.value, 24 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search134_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search134_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search134_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search134 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search134_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace134 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search147_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search147_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search147_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search147 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search147_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace147_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace147_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace147_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace147_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace147 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace147_0.value, &replace147_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search148_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search148_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search148_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search148 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search148_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace148_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace148_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace148_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace148 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace148_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search149_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search149_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search149_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search149 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search149_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace149_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
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
static const nir_search_expression replace149_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace149_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace149 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace149_0.value, &replace149_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search150_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search150_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search150_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search150_0_0.value, &search150_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search150 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search150_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace150_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace150_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace150_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace150_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace150 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace150_0.value, &replace150_1.value },
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search134, &replace134.value, 0 },
   { &search147, &replace147.value, 0 },
   { &search148, &replace148.value, 0 },
   { &search149, &replace149.value, 0 },
   { &search150, &replace150.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search56_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search56_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search56_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search56_0_0.value, &search56_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search56 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search56_0.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace56 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace56_0.value, &replace56_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search57_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search57_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search57_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search57_0_0.value, &search57_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search57 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search57_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace57_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace57_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace57 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace57_0.value, &replace57_1.value },
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
   nir_op_feq,
   { &search58_0_0.value, &search58_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search58 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search58_0.value },
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
static const nir_search_expression replace58 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace58_0.value, &replace58_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search59_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search59_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search59_0_0.value, &search59_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search59 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search59_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace59_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace59_0.value, &replace59_1.value },
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
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search60_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search60_0_0.value, &search60_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search60_0.value },
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
static const nir_search_variable replace60_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace60_0.value, &replace60_1.value },
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
   nir_op_ige,
   { &search61_0_0.value, &search61_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search61_0.value },
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
   nir_op_ilt,
   { &replace61_0.value, &replace61_1.value },
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
   nir_op_ieq,
   { &search62_0_0.value, &search62_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search62_0.value },
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
   nir_op_ine,
   { &replace62_0.value, &replace62_1.value },
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
   nir_op_ine,
   { &search63_0_0.value, &search63_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search63 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search63_0.value },
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
   nir_op_ieq,
   { &replace63_0.value, &replace63_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search122_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search122_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search122_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search122_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace122 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_inot_xforms[] = {
   { &search56, &replace56.value, 0 },
   { &search57, &replace57.value, 0 },
   { &search58, &replace58.value, 0 },
   { &search59, &replace59.value, 0 },
   { &search60, &replace60.value, 0 },
   { &search61, &replace61.value, 0 },
   { &search62, &replace62.value, 0 },
   { &search63, &replace63.value, 0 },
   { &search122, &replace122.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search97_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search97_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search97_0.value, &search97_1.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace97_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace97_0_0.value, &replace97_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace97_0.value },
};

static const struct transform nir_opt_algebraic_sne_xforms[] = {
   { &search97, &replace97.value, 11 },
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
   { nir_search_value_expression, 32 },
   false,
   nir_op_fmod,
   { &search200_0.value, &search200_1.value },
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
static const nir_search_variable replace200_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace200_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace200_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace200_1_1_0_0.value, &replace200_1_1_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace200_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace200_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace200_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace200_1_0.value, &replace200_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace200_0.value, &replace200_1.value },
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
static const nir_search_variable search201_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search201 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_fmod,
   { &search201_0.value, &search201_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace201_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace201_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace201_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace201_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace201_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace201_1_1_0_0.value, &replace201_1_1_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace201_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace201_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace201_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace201_1_0.value, &replace201_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace201_0.value, &replace201_1.value },
};

static const struct transform nir_opt_algebraic_fmod_xforms[] = {
   { &search200, &replace200.value, 21 },
   { &search201, &replace201.value, 22 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search171_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search171_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search171_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &search171_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace171_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace171_0.value },
};

static const struct transform nir_opt_algebraic_f2u_xforms[] = {
   { &search171, &replace171.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search168_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search168_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search168_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fcsel,
   { &search168_0.value, &search168_1.value, &search168_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace168 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_fcsel_xforms[] = {
   { &search168, &replace168.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search183_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search183_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search183_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search183_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search183_1_0.value, &search183_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search183_0.value, &search183_1.value },
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
static const nir_search_variable replace183_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace183_0.value, &replace183_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search187_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search187_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search187_0.value, &search187_1.value },
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
static const nir_search_variable replace187_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace187_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace187_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace187_0.value, &replace187_1.value },
};

static const struct transform nir_opt_algebraic_isub_xforms[] = {
   { &search183, &replace183.value, 0 },
   { &search187, &replace187.value, 19 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search73_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search73_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search73_0.value, &search73_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace73 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search79_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search79_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search79_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search79_0_0.value, &search79_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search79_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search79 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmax,
   { &search79_0.value, &search79_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace79_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace79 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace79_0.value },
};

static const struct transform nir_opt_algebraic_fmax_xforms[] = {
   { &search73, &replace73.value, 0 },
   { &search79, &replace79.value, 9 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search27_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search27_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search27 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search27_0.value, &search27_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace27 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
static const nir_search_constant search28_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search28 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search28_0.value, &search28_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace28 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_umul_unorm_4x8_xforms[] = {
   { &search27, &replace27.value, 0 },
   { &search28, &replace28.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search205_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search205_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search205_2 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search205_3 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_insert,
   { &search205_0.value, &search205_1.value, &search205_2.value, &search205_3.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace205_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_0_1 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace205_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace205_0_0.value, &replace205_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_2_0_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_2_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace205_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace205_2_0_0.value, &replace205_2_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace205_2_2 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace205_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfi,
   { &replace205_2_0.value, &replace205_2_1.value, &replace205_2_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace205_0.value, &replace205_1.value, &replace205_2.value },
};

static const struct transform nir_opt_algebraic_bitfield_insert_xforms[] = {
   { &search205, &replace205.value, 25 },
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
static const nir_search_expression search99_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search99_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search99_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search99_0.value, &search99_1.value },
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
static const nir_search_constant replace99_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace99_0.value, &replace99_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search227_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search227_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search227_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search227_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search227_0_0.value, &search227_0_1.value, &search227_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search227_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search227_0.value, &search227_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace227_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace227_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace227_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace227_1_0.value, &replace227_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace227_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace227_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace227_2_0.value, &replace227_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace227_0.value, &replace227_1.value, &replace227_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search228_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search228_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search228_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search228_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search228_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search228_1_0.value, &search228_1_1.value, &search228_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search228_0.value, &search228_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace228_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace228_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace228_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace228_1_0.value, &replace228_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace228_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace228_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace228_2_0.value, &replace228_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace228_0.value, &replace228_1.value, &replace228_2.value },
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search99, &replace99.value, 0 },
   { &search227, &replace227.value, 0 },
   { &search228, &replace228.value, 0 },
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
static const nir_search_variable search38_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search38_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search38 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search38_0.value, &search38_1.value, &search38_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace38 = {
   { nir_search_value_variable, 0 },
   0, /* a */
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
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search39 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search39_0.value, &search39_1.value, &search39_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace39 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
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
static const nir_search_variable search40_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
   true,
   nir_op_flrp,
   { &search40_0.value, &search40_1.value, &search40_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace40 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search41_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
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
   true,
   nir_op_flrp,
   { &search41_0.value, &search41_1.value, &search41_2.value },
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
   nir_op_fmul,
   { &replace41_0.value, &replace41_1.value },
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
static const nir_search_variable search42_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search42_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search42_2_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search42 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search42_0.value, &search42_1.value, &search42_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace42_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace42_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace42_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace42 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace42_0.value, &replace42_1.value, &replace42_2.value },
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
static const nir_search_variable search43_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search43 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_flrp,
   { &search43_0.value, &search43_1.value, &search43_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace43_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace43_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace43_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace43_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace43_0_1_0.value, &replace43_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace43_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace43_0_0.value, &replace43_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace43_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace43 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace43_0.value, &replace43_1.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search44_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search44 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_flrp,
   { &search44_0.value, &search44_1.value, &search44_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace44_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace44_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace44_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace44_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace44_0_1_0.value, &replace44_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace44_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace44_0_0.value, &replace44_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace44_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace44 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace44_0.value, &replace44_1.value },
};

static const struct transform nir_opt_algebraic_flrp_xforms[] = {
   { &search38, &replace38.value, 0 },
   { &search39, &replace39.value, 0 },
   { &search40, &replace40.value, 0 },
   { &search41, &replace41.value, 0 },
   { &search42, &replace42.value, 2 },
   { &search43, &replace43.value, 2 },
   { &search44, &replace44.value, 3 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search86_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search86_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search86_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search86_0_0.value, &search86_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search86_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search86_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search86_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search86_1_0.value, &search86_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search86 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search86_0.value, &search86_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace86_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace86_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace86_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace86_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace86_1_0.value, &replace86_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace86_0.value, &replace86_1.value },
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
static const nir_search_variable search87_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search87_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search87_0_0.value, &search87_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search87_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search87_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search87_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search87_1_0.value, &search87_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search87 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search87_0.value, &search87_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace87_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace87_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace87_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace87_0_0.value, &replace87_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace87_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace87_0.value, &replace87_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_op_fge,
   { &search88_0_0.value, &search88_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search88_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search88_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search88_1_0.value, &search88_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search88 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search88_0.value, &search88_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace88_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace88_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace88_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace88_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace88_1_0.value, &replace88_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace88_0.value, &replace88_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search89_0_0.value, &search89_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search89_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search89_1_0.value, &search89_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search89 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search89_0.value, &search89_1.value },
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
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace89_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace89_0_0.value, &replace89_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace89_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace89_0.value, &replace89_1.value },
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
   nir_op_ior,
   { &search116_0.value, &search116_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace116 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
static const nir_search_constant search117_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search117_0.value, &search117_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace117 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
static const nir_search_constant search118_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search118 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search118_0.value, &search118_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace118 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search124_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search124_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search124_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search124_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search124_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search124_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search124 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search124_0.value, &search124_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace124_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace124_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace124_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace124_0_0.value, &replace124_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace124 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace124_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search222_0_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search222_0_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_0_0_0_1_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0_0_0_0_1_0_0.value, &search222_0_0_0_0_0_0_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_0_0_0_1_0.value, &search222_0_0_0_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search222_0_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_1_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0_1_0_0_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_1_0_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search222_0_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_1_0_0_1_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0_1_0_0_1_0_0.value, &search222_0_0_0_0_0_0_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_1_0_0_1_0.value, &search222_0_0_0_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_1_0_0_0.value, &search222_0_0_0_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0_1_0_0.value, &search222_0_0_0_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_0_0_0_1_0.value, &search222_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0_0_0_0.value, &search222_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_0_0_0.value, &search222_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_0_0.value, &search222_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search222_0_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_0_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search222_0_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_0_0_0_1_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0_0_0_0_1_0_0.value, &search222_0_0_0_1_0_0_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_0_0_0_1_0.value, &search222_0_0_0_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0_0_0_0.value, &search222_0_0_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_0_0.value, &search222_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search222_0_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_1_0_0_0_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0_1_0_0_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_1_0_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_0_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search222_0_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_1_0_0_1_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0_1_0_0_1_0_0.value, &search222_0_0_0_1_0_0_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_1_0_0_1_0.value, &search222_0_0_0_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_1_0_0_0.value, &search222_0_0_0_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0_1_0_0.value, &search222_0_0_0_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0_0_1_0.value, &search222_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_1_0_0_0.value, &search222_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0_1_0_0.value, &search222_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_0_0_0_1_0.value, &search222_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0_0_0_0.value, &search222_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x55555555 /* 1431655765 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_0_0_0.value, &search222_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_0_0.value, &search222_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search222_1_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search222_1_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_0_0_0_1_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0_0_0_0_1_0_0.value, &search222_1_0_0_0_0_0_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_0_0_0_1_0.value, &search222_1_0_0_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search222_1_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_1_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0_1_0_0_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_1_0_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search222_1_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_1_0_0_1_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0_1_0_0_1_0_0.value, &search222_1_0_0_0_0_0_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_1_0_0_1_0.value, &search222_1_0_0_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_1_0_0_0.value, &search222_1_0_0_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0_1_0_0.value, &search222_1_0_0_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_0_0_0_1_0.value, &search222_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0_0_0_0.value, &search222_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_0_0_0.value, &search222_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_0_0.value, &search222_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search222_1_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_0_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search222_1_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_0_0_0_1_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0_0_0_0_1_0_0.value, &search222_1_0_0_1_0_0_0_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_0_0_0_1_0.value, &search222_1_0_0_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0_0_0_0.value, &search222_1_0_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_0_0.value, &search222_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search222_1_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_1_0_0_0_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0_1_0_0_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_1_0_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search222_1_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search222_1_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search222_1_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_1_0_0_1_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0_1_0_0_1_0_0.value, &search222_1_0_0_1_0_0_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_1_0_0_1_0.value, &search222_1_0_0_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_1_0_0_0.value, &search222_1_0_0_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0_1_0_0.value, &search222_1_0_0_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0_0_1_0.value, &search222_1_0_0_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_1_0_0_0.value, &search222_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0_1_0_0.value, &search222_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0_0_1_0.value, &search222_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_1_0_0_0.value, &search222_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xaaaaaaaa /* 2863311530 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search222_1_0_0.value, &search222_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search222_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search222_1_0.value, &search222_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search222_0.value, &search222_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace222_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_reverse,
   { &replace222_0.value },
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search86, &replace86.value, 0 },
   { &search87, &replace87.value, 0 },
   { &search88, &replace88.value, 0 },
   { &search89, &replace89.value, 0 },
   { &search116, &replace116.value, 0 },
   { &search117, &replace117.value, 0 },
   { &search118, &replace118.value, 0 },
   { &search124, &replace124.value, 0 },
   { &search222, &replace222.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search184_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search184_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search184_0.value, &search184_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace184 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search185_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search185_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search185_0.value, &search185_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace185 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ussub_4x8_xforms[] = {
   { &search184, &replace184.value, 0 },
   { &search185, &replace185.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search219_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_4x8,
   { &search219_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace219_1_1_0_0_0_0.value, &replace219_1_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace219_1_1_0_0_1_0.value, &replace219_1_1_0_0_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_1_1_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_1_1_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace219_1_1_0_0_2_0.value, &replace219_1_1_0_0_2_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace219_1_1_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_1_1_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace219_1_1_0_0_3_0.value, &replace219_1_1_0_0_3_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace219_1_1_0_0_0.value, &replace219_1_1_0_0_1.value, &replace219_1_1_0_0_2.value, &replace219_1_1_0_0_3.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f,
   { &replace219_1_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace219_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace219_1_1_0.value, &replace219_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace219_1_0.value, &replace219_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace219_0.value, &replace219_1.value },
};

static const struct transform nir_opt_algebraic_unpack_snorm_4x8_xforms[] = {
   { &search219, &replace219.value, 36 },
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
static const nir_search_variable search202_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frem,
   { &search202_0.value, &search202_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace202_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace202_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace202_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace202_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace202_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace202_1_1_0_0.value, &replace202_1_1_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace202_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &replace202_1_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace202_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace202_1_0.value, &replace202_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace202_0.value, &replace202_1.value },
};

static const struct transform nir_opt_algebraic_frem_xforms[] = {
   { &search202, &replace202.value, 21 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search216_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_2x16,
   { &search216_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace216_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace216_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace216_0_0_0_0.value, &replace216_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace216_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace216_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace216_0_0_1_0.value, &replace216_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace216_0_0_0.value, &replace216_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace216_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace216_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace216_0.value, &replace216_1.value },
};

static const struct transform nir_opt_algebraic_unpack_unorm_2x16_xforms[] = {
   { &search216, &replace216.value, 33 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search218_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_2x16,
   { &search218_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace218_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace218_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace218_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace218_1_1_0_0_0_0.value, &replace218_1_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace218_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace218_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace218_1_1_0_0_1_0.value, &replace218_1_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace218_1_1_0_0_0.value, &replace218_1_1_0_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f,
   { &replace218_1_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace218_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace218_1_1_0.value, &replace218_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace218_1_0.value, &replace218_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace218_0.value, &replace218_1.value },
};

static const struct transform nir_opt_algebraic_unpack_snorm_2x16_xforms[] = {
   { &search218, &replace218.value, 35 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search75_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search75_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search75_0.value, &search75_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace75 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_imax_xforms[] = {
   { &search75, &replace75.value, 0 },
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
static const nir_search_expression search80 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search80_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace80_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace80_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace80_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace80_0_0.value, &replace80_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace80_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace80 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace80_0.value, &replace80_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search81_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search81_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search81_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search81 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search81_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace81_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace81 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace81_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search102_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search102_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search102_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search102_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search102_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search102_0_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search102_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search102_0_0.value, &search102_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search102_0.value },
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
   nir_op_ior,
   { &replace102_0_0.value, &replace102_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace102_0.value },
};

static const struct transform nir_opt_algebraic_fsat_xforms[] = {
   { &search80, &replace80.value, 10 },
   { &search81, &replace81.value, 0 },
   { &search102, &replace102.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search211_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search211_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &search211_0.value, &search211_1.value },
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
static const nir_search_variable replace211_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace211_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace211_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace211_0_1_0.value, &replace211_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace211_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace211_0_0.value, &replace211_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace211_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace211_0.value, &replace211_1.value },
};

static const struct transform nir_opt_algebraic_extract_u16_xforms[] = {
   { &search211, &replace211.value, 28 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search64_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search64_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search64_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search64_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search64_0.value, &search64_1.value },
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
static const nir_search_expression replace64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace64_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search66_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search66_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search66_0_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search66_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search66_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search66_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search66_0.value, &search66_1.value },
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
static const nir_search_constant replace66_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace66_0.value, &replace66_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search225_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search225_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search225_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search225_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search225_0_0.value, &search225_0_1.value, &search225_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search225_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search225_0.value, &search225_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace225_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace225_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace225_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace225_1_0.value, &replace225_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace225_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace225_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace225_2_0.value, &replace225_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace225_0.value, &replace225_1.value, &replace225_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search226_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search226_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search226_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search226_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search226_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search226_1_0.value, &search226_1_1.value, &search226_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search226_0.value, &search226_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace226_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace226_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace226_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace226_1_0.value, &replace226_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace226_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace226_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace226_2_0.value, &replace226_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace226_0.value, &replace226_1.value, &replace226_2.value },
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search64, &replace64.value, 0 },
   { &search66, &replace66.value, 0 },
   { &search225, &replace225.value, 0 },
   { &search226, &replace226.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search145_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search145_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search145 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search145_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace145_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace145_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace145 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace145_0.value },
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
   nir_op_frcp,
   { &search154_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search154 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search154_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace154 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
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
   nir_op_fsqrt,
   { &search155_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search155 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search155_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace155_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace155 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace155_0.value },
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
   nir_op_frsq,
   { &search157_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search157 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search157_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace157_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace157 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &replace157_0.value },
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search145, &replace145.value, 0 },
   { &search154, &replace154.value, 0 },
   { &search155, &replace155.value, 0 },
   { &search157, &replace157.value, 16 },
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
   nir_op_fxor,
   { &search119_0.value, &search119_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace119 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search119, &replace119.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search129_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
   nir_op_ushr,
   { &search129_0.value, &search129_1.value },
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
   nir_op_ushr,
   { &search130_0.value, &search130_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace130 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search176 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search176_0.value, &search176_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace176_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace176_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace176 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace176_0.value, &replace176_1.value },
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
static const nir_search_constant search180_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search180_0.value, &search180_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace180_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace180_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace180_0.value, &replace180_1.value },
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search129, &replace129.value, 0 },
   { &search130, &replace130.value, 0 },
   { &search176, &replace176.value, 17 },
   { &search180, &replace180.value, 18 },
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
   nir_op_flog2,
   { &search133_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search133 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search133_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace133 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search136_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search136_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search136_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search136_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search136_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search136_0_0.value, &search136_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search136 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search136_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace136_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace136_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace136 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace136_0.value, &replace136_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search137_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search137_0_0_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search137_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search137_0_0_0.value, &search137_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search137_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search137_0_1_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search137_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search137_0_1_0.value, &search137_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search137_0_0.value, &search137_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search137 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search137_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace137_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace137_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace137_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace137_0_0.value, &replace137_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace137_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace137_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace137_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace137_1_0.value, &replace137_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace137 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &replace137_0.value, &replace137_1.value },
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search133, &replace133.value, 0 },
   { &search136, &replace136.value, 13 },
   { &search137, &replace137.value, 13 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search127_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search127_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search127 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search127_0.value, &search127_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace127 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
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
static const nir_search_constant search128_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search128 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search128_0.value, &search128_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace128 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search127, &replace127.value, 0 },
   { &search128, &replace128.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search94_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search94_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search94_0.value, &search94_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace94_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace94_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace94_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace94_0_0.value, &replace94_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace94_0.value },
};

static const struct transform nir_opt_algebraic_slt_xforms[] = {
   { &search94, &replace94.value, 11 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search170_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search170_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search170_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &search170_0.value },
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
   nir_op_f2i,
   { &replace170_0.value },
};

static const struct transform nir_opt_algebraic_f2i_xforms[] = {
   { &search170, &replace170.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search65_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search65_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search65_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search65_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search65_0.value, &search65_1.value },
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
static const nir_search_constant replace65_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace65_0.value, &replace65_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search104_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search104_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search104_0_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search104_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search104_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search104_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search104_0.value, &search104_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace104 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search105_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search105_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search105_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search105_0_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search105_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search105_0_0.value, &search105_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search105_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search105_0.value, &search105_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace105 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search223_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search223_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search223_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search223_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search223_0_0.value, &search223_0_1.value, &search223_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search223_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search223_0.value, &search223_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace223_1_0.value, &replace223_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace223_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace223_2_0.value, &replace223_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace223_0.value, &replace223_1.value, &replace223_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search224_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search224_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search224_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search224_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search224_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search224_1_0.value, &search224_1_1.value, &search224_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search224_0.value, &search224_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace224_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace224_1_0.value, &replace224_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace224_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace224_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace224_2_0.value, &replace224_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace224_0.value, &replace224_1.value, &replace224_2.value },
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search65, &replace65.value, 0 },
   { &search104, &replace104.value, 0 },
   { &search105, &replace105.value, 0 },
   { &search223, &replace223.value, 0 },
   { &search224, &replace224.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search110_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search110_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search110_0.value, &search110_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace110 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
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
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search239_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search239_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search239_0_0.value, &search239_0_1.value, &search239_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search239_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search239_0.value, &search239_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace239_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace239_1_0.value, &replace239_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace239_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace239_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace239_2_0.value, &replace239_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace239_0.value, &replace239_1.value, &replace239_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search240_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search240_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search240_1_0.value, &search240_1_1.value, &search240_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search240_0.value, &search240_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace240_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace240_1_0.value, &replace240_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace240_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace240_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace240_2_0.value, &replace240_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace240_0.value, &replace240_1.value, &replace240_2.value },
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search110, &replace110.value, 0 },
   { &search239, &replace239.value, 0 },
   { &search240, &replace240.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search182_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search182_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search182_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search182_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search182_1_0.value, &search182_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search182 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsub,
   { &search182_0.value, &search182_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace182_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace182_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace182_0.value, &replace182_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search186_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search186_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search186_0.value, &search186_1.value },
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
static const nir_search_variable replace186_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace186_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace186_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace186_0.value, &replace186_1.value },
};

static const struct transform nir_opt_algebraic_fsub_xforms[] = {
   { &search182, &replace182.value, 0 },
   { &search186, &replace186.value, 19 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search6_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search6_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search6_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search6 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search6_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace6 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search188_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search188 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search188_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace188_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace188_1 = {
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
   nir_op_fsub,
   { &replace188_0.value, &replace188_1.value },
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search6, &replace6.value, 0 },
   { &search188, &replace188.value, 20 },
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
static const nir_search_expression search98_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search98_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search98_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search98_0.value, &search98_1.value },
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
static const nir_search_constant replace98_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace98_0.value, &replace98_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search229_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search229_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search229_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search229_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search229_0_0.value, &search229_0_1.value, &search229_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search229_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search229_0.value, &search229_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace229_1_0.value, &replace229_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace229_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace229_2_0.value, &replace229_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace229_0.value, &replace229_1.value, &replace229_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search230_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search230_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search230_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search230_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search230_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search230_1_0.value, &search230_1_1.value, &search230_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search230_0.value, &search230_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace230_1_0.value, &replace230_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace230_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace230_2_0.value, &replace230_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace230_0.value, &replace230_1.value, &replace230_2.value },
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search98, &replace98.value, 0 },
   { &search229, &replace229.value, 0 },
   { &search230, &replace230.value, 0 },
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
   nir_op_iabs,
   { &search11_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search11 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search11_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace11_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace11 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace11_0.value },
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
   nir_op_ineg,
   { &search12_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search12 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search12_0.value },
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
   nir_op_iabs,
   { &replace12_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search175_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search175_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search175_0_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search175 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search175_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace175_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace175 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace175_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search193_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search193_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search193_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search193_0_0.value, &search193_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search193_0.value },
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
static const nir_search_expression replace193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace193_0.value },
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search11, &replace11.value, 0 },
   { &search12, &replace12.value, 0 },
   { &search175, &replace175.value, 0 },
   { &search193, &replace193.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search217_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_4x8,
   { &search217_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace217_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace217_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace217_0_0_0_0.value, &replace217_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace217_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace217_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace217_0_0_1_0.value, &replace217_0_0_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace217_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace217_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace217_0_0_2_0.value, &replace217_0_0_2_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace217_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace217_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace217_0_0_3_0.value, &replace217_0_0_3_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace217_0_0_0.value, &replace217_0_0_1.value, &replace217_0_0_2.value, &replace217_0_0_3.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace217_0_0.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace217_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace217_0.value, &replace217_1.value },
};

static const struct transform nir_opt_algebraic_unpack_unorm_4x8_xforms[] = {
   { &search217, &replace217.value, 34 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search72_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search72_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search72_0.value, &search72_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace72 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search78_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search78_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search78_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search78_0_0.value, &search78_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search78_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search78 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmin,
   { &search78_0.value, &search78_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace78_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace78 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace78_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search82_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search82_0_0_0_0.value, &search82_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search82_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search82_0_0_0.value, &search82_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search82_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search82_0_0.value, &search82_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search82_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search82 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search82_0.value, &search82_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace82_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace82_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace82_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace82_0_0.value, &replace82_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace82_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace82 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace82_0.value, &replace82_1.value },
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search72, &replace72.value, 0 },
   { &search78, &replace78.value, 9 },
   { &search82, &replace82.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search109_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search109_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search109 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search109_0.value, &search109_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace109 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search159_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search159_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search159 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search159_0.value, &search159_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace159_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace159 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace159_0.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search160_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search160_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search160_0.value, &search160_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace160 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search237_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search237_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search237_0_0.value, &search237_0_1.value, &search237_0_2.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search237_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search237_0.value, &search237_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace237_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace237_1_0.value, &replace237_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace237_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace237_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace237_2_0.value, &replace237_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace237_0.value, &replace237_1.value, &replace237_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search238_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search238_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search238_1_0.value, &search238_1_1.value, &search238_1_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search238_0.value, &search238_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace238_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace238_1_0.value, &replace238_1_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace238_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace238_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace238_2_0.value, &replace238_2_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace238_0.value, &replace238_1.value, &replace238_2.value },
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search109, &replace109.value, 0 },
   { &search159, &replace159.value, 0 },
   { &search160, &replace160.value, 0 },
   { &search237, &replace237.value, 0 },
   { &search238, &replace238.value, 0 },
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
static const nir_search_variable search2_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_udiv,
   { &search2_0.value, &search2_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace2_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace2_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace2_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace2_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace2_0.value, &replace2_1.value },
};

static const struct transform nir_opt_algebraic_udiv_xforms[] = {
   { &search2, &replace2.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search208_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_op_extract_i8,
   { &search208_0.value, &search208_1.value },
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
static const nir_search_constant replace208_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace208_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace208_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace208_0_1_0_0.value, &replace208_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace208_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace208_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace208_0_1_0.value, &replace208_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace208_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace208_0_0.value, &replace208_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace208_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace208_0.value, &replace208_1.value },
};

static const struct transform nir_opt_algebraic_extract_i8_xforms[] = {
   { &search208, &replace208.value, 27 },
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
static const nir_search_variable search203_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search203 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_uadd_carry,
   { &search203_0.value, &search203_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace203_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace203_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace203_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace203_0_0_0.value, &replace203_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace203_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace203_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace203_0_0.value, &replace203_0_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace203_0.value },
};

static const struct transform nir_opt_algebraic_uadd_carry_xforms[] = {
   { &search203, &replace203.value, 23 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search206_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search206_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search206_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibitfield_extract,
   { &search206_0.value, &search206_1.value, &search206_2.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace206_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace206_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace206_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace206_0_0.value, &replace206_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace206_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace206_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace206_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace206_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace206_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibfe,
   { &replace206_2_0.value, &replace206_2_1.value, &replace206_2_2.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace206_0.value, &replace206_1.value, &replace206_2.value },
};

static const struct transform nir_opt_algebraic_ibitfield_extract_xforms[] = {
   { &search206, &replace206.value, 26 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search220_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search220_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search220 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ldexp,
   { &search220_0.value, &search220_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_0_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace220_0_1_0_0_0_0_0.value, &replace220_0_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace220_0_1_0_0_0_0.value, &replace220_0_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace220_0_1_0_0_0.value, &replace220_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace220_0_1_0_0.value, &replace220_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace220_0_1_0.value, &replace220_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace220_0_0.value, &replace220_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace220_1_0_0_0_0_0.value, &replace220_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace220_1_0_0_0_0.value, &replace220_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace220_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace220_1_0_0_1_0_0_0.value, &replace220_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace220_1_0_0_1_0_0.value, &replace220_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace220_1_0_0_1_0.value, &replace220_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace220_1_0_0_0.value, &replace220_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace220_1_0_0.value, &replace220_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace220_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace220_1_0.value, &replace220_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace220_0.value, &replace220_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search221_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search221_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search221 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_ldexp,
   { &search221_0.value, &search221_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace221_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace221_0_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace221_0_1_1_0_0_0_0_0.value, &replace221_0_1_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace221_0_1_1_0_0_0_0.value, &replace221_0_1_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace221_0_1_1_0_0_0.value, &replace221_0_1_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace221_0_1_1_0_0.value, &replace221_0_1_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_0_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace221_0_1_1_0.value, &replace221_0_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_double_2x32_split,
   { &replace221_0_1_0.value, &replace221_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace221_0_0.value, &replace221_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace221_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace221_1_1_0_0_0_0_0.value, &replace221_1_1_0_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace221_1_1_0_0_0_0.value, &replace221_1_1_0_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace221_1_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace221_1_1_0_0_1_0_0_0.value, &replace221_1_1_0_0_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace221_1_1_0_0_1_0_0.value, &replace221_1_1_0_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace221_1_1_0_0_1_0.value, &replace221_1_1_0_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace221_1_1_0_0_0.value, &replace221_1_1_0_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace221_1_1_0_0.value, &replace221_1_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace221_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace221_1_1_0.value, &replace221_1_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_double_2x32_split,
   { &replace221_1_0.value, &replace221_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace221_0.value, &replace221_1.value },
};

static const struct transform nir_opt_algebraic_ldexp_xforms[] = {
   { &search220, &replace220.value, 0 },
   { &search221, &replace221.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search210_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search210_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &search210_0.value, &search210_1.value },
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
static const nir_search_constant replace210_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace210_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace210_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace210_0_1_0_0.value, &replace210_0_1_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace210_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace210_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace210_0_1_0.value, &replace210_0_1_1.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace210_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace210_0_0.value, &replace210_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant replace210_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace210_0.value, &replace210_1.value },
};

static const struct transform nir_opt_algebraic_extract_i16_xforms[] = {
   { &search210, &replace210.value, 28 },
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
static const nir_search_variable search249_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search249_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search249_0.value, &search249_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace249_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated4,
   { &replace249_0.value, &replace249_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot4_xforms[] = {
   { &search249, &replace249.value, 37 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search243_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search243_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search243_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search243_0_0.value, &search243_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search243_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search243_0.value, &search243_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace243_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace243_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace243_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace243_0.value, &replace243_1.value },
};

static const struct transform nir_opt_algebraic_late_flt_xforms[] = {
   { &search243, &replace243.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search247_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search247_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot2,
   { &search247_0.value, &search247_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace247_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated2,
   { &replace247_0.value, &replace247_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot2_xforms[] = {
   { &search247, &replace247.value, 37 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search248_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search248_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search248_0.value, &search248_1.value },
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
static const nir_search_variable replace248_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated3,
   { &replace248_0.value, &replace248_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot3_xforms[] = {
   { &search248, &replace248.value, 37 },
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
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search246_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search246_0_0.value, &search246_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search246_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search246 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fne,
   { &search246_0.value, &search246_1.value },
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
static const nir_search_expression replace246_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace246_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace246_0.value, &replace246_1.value },
};

static const struct transform nir_opt_algebraic_late_fne_xforms[] = {
   { &search246, &replace246.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search245_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search245_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search245_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search245_0_0.value, &search245_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search245_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search245 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_feq,
   { &search245_0.value, &search245_1.value },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable replace245_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace245_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace245_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace245 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace245_0.value, &replace245_1.value },
};

static const struct transform nir_opt_algebraic_late_feq_xforms[] = {
   { &search245, &replace245.value, 0 },
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
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search244_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search244_0_0.value, &search244_0_1.value },
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_constant search244_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search244 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search244_0.value, &search244_1.value },
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
static const nir_search_expression replace244_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace244_1_0.value },
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace244 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace244_0.value, &replace244_1.value },
};

static const struct transform nir_opt_algebraic_late_fge_xforms[] = {
   { &search244, &replace244.value, 0 },
};
   
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search250_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

#include "compiler/nir/nir_search_helpers.h"
static const nir_search_variable search250_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression search250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph,
   { &search250_0.value, &search250_1.value },
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
static const nir_search_variable replace250_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
#include "compiler/nir/nir_search_helpers.h"
static const nir_search_expression replace250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph_replicated,
   { &replace250_0.value, &replace250_1.value },
};

static const struct transform nir_opt_algebraic_late_fdph_xforms[] = {
   { &search250, &replace250.value, 37 },
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

