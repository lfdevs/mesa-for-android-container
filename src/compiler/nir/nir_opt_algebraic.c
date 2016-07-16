
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

   
static const nir_search_variable search94_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
};

static const nir_search_constant search94_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search94_0.value, &search94_1.value },
};
   
static const nir_search_variable replace94_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace94_0.value },
};
   
static const nir_search_variable search104_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search104_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search104_0.value, &search104_1.value },
};
   
static const nir_search_variable replace104 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search105_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search105_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search105_0.value, &search105_1.value },
};
   
static const nir_search_variable replace105 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search106_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search106_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search106 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search106_0.value, &search106_1.value },
};
   
static const nir_search_constant replace106 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search113_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search113_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search113_0_0.value },
};

static const nir_search_variable search113_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search113_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search113_1_0.value },
};
static const nir_search_expression search113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search113_0.value, &search113_1.value },
};
   
static const nir_search_variable replace113_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace113_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace113_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace113_0_0.value, &replace113_0_1.value },
};
static const nir_search_expression replace113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace113_0.value },
};
   
static const nir_search_constant search121_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search121_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search121_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression search121_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search121_1_0.value, &search121_1_1.value },
};
static const nir_search_expression search121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search121_0.value, &search121_1.value },
};
   
static const nir_search_variable replace121_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace121_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression replace121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace121_0.value, &replace121_1.value },
};
   
static const nir_search_constant search122_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

static const nir_search_variable search122_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search122_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search122_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search122_1_0.value, &search122_1_1.value },
};
static const nir_search_expression search122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search122_0.value, &search122_1.value },
};
   
static const nir_search_variable replace122_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace122_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace122_0.value, &replace122_1.value },
};
   
static const nir_search_constant search167_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search167_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search167_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search167_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search167_1_0.value, &search167_1_1.value },
};
static const nir_search_expression search167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search167_0.value, &search167_1.value },
};
   
static const nir_search_variable replace167_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace167_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace167_0.value, &replace167_1.value },
};
   
static const nir_search_constant search168_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search168_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search168_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search168_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search168_1_0.value, &search168_1_1.value },
};
static const nir_search_expression search168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search168_0.value, &search168_1.value },
};
   
static const nir_search_variable replace168_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace168_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace168_0.value, &replace168_1.value },
};
   
static const nir_search_constant search169_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search169_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search169 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search169_0.value, &search169_1.value },
};
   
static const nir_search_variable replace169_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace169_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace169 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace169_0.value, &replace169_1.value },
};
   
static const nir_search_constant search171_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

static const nir_search_variable search171_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search171_0.value, &search171_1.value },
};
   
static const nir_search_variable replace171_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace171_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace171_0.value, &replace171_1.value },
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search94, &replace94.value, 0 },
   { &search104, &replace104.value, 0 },
   { &search105, &replace105.value, 0 },
   { &search106, &replace106.value, 0 },
   { &search113, &replace113.value, 0 },
   { &search121, &replace121.value, 0 },
   { &search122, &replace122.value, 0 },
   { &search167, &replace167.value, 16 },
   { &search168, &replace168.value, 16 },
   { &search169, &replace169.value, 16 },
   { &search171, &replace171.value, 17 },
};
   
static const nir_search_variable search110_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search110_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search110_0.value, &search110_1.value },
};
   
static const nir_search_constant replace110 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search111_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search111_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search111_0.value, &search111_1.value },
};
   
static const nir_search_variable replace111 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search110, &replace110.value, 0 },
   { &search111, &replace111.value, 0 },
};
   
static const nir_search_variable search87_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search87_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search87_0.value, &search87_1.value },
};
   
static const nir_search_variable replace87_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace87_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace87_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace87_0_0.value, &replace87_0_1.value },
};
static const nir_search_expression replace87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace87_0.value },
};

static const struct transform nir_opt_algebraic_seq_xforms[] = {
   { &search87, &replace87.value, 10 },
};
   
static const nir_search_variable search97_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search97_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search97_0.value, &search97_1.value },
};
   
static const nir_search_constant replace97 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search221_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search221_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search221_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search221_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search221_0_0.value, &search221_0_1.value, &search221_0_2.value },
};

static const nir_search_variable search221_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search221_0.value, &search221_1.value },
};
   
static const nir_search_variable replace221_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace221_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace221_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace221_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace221_1_0.value, &replace221_1_1.value },
};

static const nir_search_variable replace221_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace221_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace221_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace221_2_0.value, &replace221_2_1.value },
};
static const nir_search_expression replace221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace221_0.value, &replace221_1.value, &replace221_2.value },
};
   
static const nir_search_variable search222_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search222_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search222_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search222_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search222_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search222_1_0.value, &search222_1_1.value, &search222_1_2.value },
};
static const nir_search_expression search222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search222_0.value, &search222_1.value },
};
   
static const nir_search_variable replace222_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace222_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace222_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace222_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace222_1_0.value, &replace222_1_1.value },
};

static const nir_search_variable replace222_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace222_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace222_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace222_2_0.value, &replace222_2_1.value },
};
static const nir_search_expression replace222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace222_0.value, &replace222_1.value, &replace222_2.value },
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search97, &replace97.value, 0 },
   { &search221, &replace221.value, 0 },
   { &search222, &replace222.value, 0 },
};
   
static const nir_search_variable search19_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search19_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search19 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search19_0.value, &search19_1.value },
};
   
static const nir_search_constant replace19 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search23_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search23_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search23 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search23_0.value, &search23_1.value },
};
   
static const nir_search_variable replace23 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search25_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search25_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search25 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search25_0.value, &search25_1.value },
};
   
static const nir_search_variable replace25_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace25 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace25_0.value },
};
   
static const nir_search_variable search91_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search91_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search91_0_0.value },
};

static const nir_search_variable search91_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search91_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search91_1_0.value },
};
static const nir_search_expression search91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search91_0.value, &search91_1.value },
};
   
static const nir_search_variable replace91_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace91_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace91_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace91_0_0.value, &replace91_0_1.value },
};
static const nir_search_expression replace91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace91_0.value },
};
   
static const nir_search_variable search185_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search185_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search185_0_0.value },
};

static const nir_search_variable search185_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search185_0.value, &search185_1.value },
};
   
static const nir_search_variable replace185_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace185_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace185_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace185_0_0.value, &replace185_0_1.value },
};
static const nir_search_expression replace185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace185_0.value },
};
   
static const nir_search_variable search187_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search187_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search187_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search187_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search187_1_0.value, &search187_1_1.value },
};
static const nir_search_expression search187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search187_0.value, &search187_1.value },
};
   
static const nir_search_variable replace187_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace187_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace187_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace187_0_0.value, &replace187_0_1.value },
};

static const nir_search_variable replace187_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace187_0.value, &replace187_1.value },
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search19, &replace19.value, 0 },
   { &search23, &replace23.value, 0 },
   { &search25, &replace25.value, 0 },
   { &search91, &replace91.value, 0 },
   { &search185, &replace185.value, 0 },
   { &search187, &replace187.value, 0 },
};
   
static const nir_search_variable search102_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search102_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search102_0.value, &search102_1.value },
};
   
static const nir_search_constant replace102 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search231_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search231_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search231_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search231_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search231_0_0.value, &search231_0_1.value, &search231_0_2.value },
};

static const nir_search_variable search231_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search231_0.value, &search231_1.value },
};
   
static const nir_search_variable replace231_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace231_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace231_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace231_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace231_1_0.value, &replace231_1_1.value },
};

static const nir_search_variable replace231_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace231_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace231_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace231_2_0.value, &replace231_2_1.value },
};
static const nir_search_expression replace231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace231_0.value, &replace231_1.value, &replace231_2.value },
};
   
static const nir_search_variable search232_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search232_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search232_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search232_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search232_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search232_1_0.value, &search232_1_1.value, &search232_1_2.value },
};
static const nir_search_expression search232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search232_0.value, &search232_1.value },
};
   
static const nir_search_variable replace232_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace232_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace232_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace232_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace232_1_0.value, &replace232_1_1.value },
};

static const nir_search_variable replace232_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace232_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace232_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace232_2_0.value, &replace232_2_1.value },
};
static const nir_search_expression replace232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace232_0.value, &replace232_1.value, &replace232_2.value },
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search102, &replace102.value, 0 },
   { &search231, &replace231.value, 0 },
   { &search232, &replace232.value, 0 },
};
   
static const nir_search_variable search8_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search8_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search8 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search8_0.value, &search8_1.value },
};
   
static const nir_search_variable replace8 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search9_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search9_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search9 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search9_0.value, &search9_1.value },
};
   
static const nir_search_constant replace9 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};

static const struct transform nir_opt_algebraic_usadd_4x8_xforms[] = {
   { &search8, &replace8.value, 0 },
   { &search9, &replace9.value, 0 },
};
   
static const nir_search_variable search18_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search18_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search18 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search18_0.value, &search18_1.value },
};
   
static const nir_search_constant replace18 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search22_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search22_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search22 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search22_0.value, &search22_1.value },
};
   
static const nir_search_variable replace22 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search24_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search24_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
static const nir_search_expression search24 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search24_0.value, &search24_1.value },
};
   
static const nir_search_variable replace24_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace24 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace24_0.value },
};
   
static const nir_search_variable search92_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search92_0_0.value },
};

static const nir_search_variable search92_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search92_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search92_1_0.value },
};
static const nir_search_expression search92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search92_0.value, &search92_1.value },
};
   
static const nir_search_variable replace92_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace92_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace92_0_0.value, &replace92_0_1.value },
};
static const nir_search_expression replace92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace92_0.value },
};
   
static const nir_search_variable search141_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search141_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search141_0_0.value },
};

static const nir_search_variable search141_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search141_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search141_1_0.value },
};
static const nir_search_expression search141 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search141_0.value, &search141_1.value },
};
   
static const nir_search_variable replace141_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace141_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace141_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace141_0_0.value, &replace141_0_1.value },
};
static const nir_search_expression replace141 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace141_0.value },
};
   
static const nir_search_variable search184_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search184_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search184_0_0.value },
};

static const nir_search_variable search184_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search184_0.value, &search184_1.value },
};
   
static const nir_search_variable replace184_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace184_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace184_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace184_0_0.value, &replace184_0_1.value },
};
static const nir_search_expression replace184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace184_0.value },
};
   
static const nir_search_variable search186_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search186_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search186_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search186_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search186_1_0.value, &search186_1_1.value },
};
static const nir_search_expression search186 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search186_0.value, &search186_1.value },
};
   
static const nir_search_variable replace186_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace186_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace186_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace186_0_0.value, &replace186_0_1.value },
};

static const nir_search_variable replace186_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace186_0.value, &replace186_1.value },
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search18, &replace18.value, 0 },
   { &search22, &replace22.value, 0 },
   { &search24, &replace24.value, 0 },
   { &search92, &replace92.value, 0 },
   { &search141, &replace141.value, 0 },
   { &search184, &replace184.value, 0 },
   { &search186, &replace186.value, 0 },
};
   
static const nir_search_variable search197_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
};

static const nir_search_variable search197_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
};

static const nir_search_variable search197_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression search197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubitfield_extract,
   { &search197_0.value, &search197_1.value, &search197_2.value },
};
   
static const nir_search_constant replace197_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace197_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace197_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace197_0_0.value, &replace197_0_1.value },
};

static const nir_search_variable replace197_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace197_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace197_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace197_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace197_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubfe,
   { &replace197_2_0.value, &replace197_2_1.value, &replace197_2_2.value },
};
static const nir_search_expression replace197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace197_0.value, &replace197_1.value, &replace197_2.value },
};

static const struct transform nir_opt_algebraic_ubitfield_extract_xforms[] = {
   { &search197, &replace197.value, 25 },
};
   
static const nir_search_constant search26_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search26_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search26_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search26 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search26_0.value, &search26_1.value, &search26_2.value },
};
   
static const nir_search_variable replace26 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search27_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search27_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search27_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search27 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search27_0.value, &search27_1.value, &search27_2.value },
};
   
static const nir_search_variable replace27 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search28_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search28_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search28_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search28 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search28_0.value, &search28_1.value, &search28_2.value },
};
   
static const nir_search_variable replace28_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace28_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace28 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace28_0.value, &replace28_1.value },
};
   
static const nir_search_variable search29_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search29_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search29_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search29 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search29_0.value, &search29_1.value, &search29_2.value },
};
   
static const nir_search_variable replace29_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace29_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace29 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace29_0.value, &replace29_1.value },
};
   
static const nir_search_constant search30_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search30_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search30_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search30 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search30_0.value, &search30_1.value, &search30_2.value },
};
   
static const nir_search_variable replace30_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace30_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace30 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace30_0.value, &replace30_1.value },
};
   
static const nir_search_variable search45_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search45_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search45_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search45 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search45_0.value, &search45_1.value, &search45_2.value },
};
   
static const nir_search_variable replace45_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace45_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace45_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace45_0_0.value, &replace45_0_1.value },
};

static const nir_search_variable replace45_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace45 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace45_0.value, &replace45_1.value },
};

static const struct transform nir_opt_algebraic_ffma_xforms[] = {
   { &search26, &replace26.value, 0 },
   { &search27, &replace27.value, 0 },
   { &search28, &replace28.value, 0 },
   { &search29, &replace29.value, 0 },
   { &search30, &replace30.value, 0 },
   { &search45, &replace45.value, 6 },
};
   
static const nir_search_variable search67_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search67_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search67 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search67_0.value, &search67_1.value },
};
   
static const nir_search_variable replace67 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search75_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search75_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search75_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search75_0_0_0_0.value, &search75_0_0_0_1.value },
};

static const nir_search_variable search75_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search75_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search75_0_0_0.value, &search75_0_0_1.value },
};

static const nir_search_variable search75_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search75_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search75_0_0.value, &search75_0_1.value },
};

static const nir_search_variable search75_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search75_0.value, &search75_1.value },
};
   
static const nir_search_variable replace75_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace75_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace75_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace75_0_0.value, &replace75_0_1.value },
};

static const nir_search_variable replace75_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace75_0.value, &replace75_1.value },
};

static const struct transform nir_opt_algebraic_umin_xforms[] = {
   { &search67, &replace67.value, 0 },
   { &search75, &replace75.value, 0 },
};
   
static const nir_search_variable search68_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search68_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search68 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search68_0.value, &search68_1.value },
};
   
static const nir_search_variable replace68 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_umax_xforms[] = {
   { &search68, &replace68.value, 0 },
};
   
static const nir_search_variable search58_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search58_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search58_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search58_0_0.value, &search58_0_1.value },
};

static const nir_search_variable search58_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search58_2 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search58 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search58_0.value, &search58_1.value, &search58_2.value },
};
   
static const nir_search_variable replace58_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace58_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace58 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace58_0.value, &replace58_1.value },
};
   
static const nir_search_variable search59_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search59_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search59_0_0.value, &search59_0_1.value },
};

static const nir_search_variable search59_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search59_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search59_0.value, &search59_1.value, &search59_2.value },
};
   
static const nir_search_variable replace59_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace59_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace59_0.value, &replace59_1.value },
};
   
static const nir_search_variable search60_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
};
static const nir_search_expression search60_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search60_0_0.value },
};

static const nir_search_variable search60_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search60_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search60_0.value, &search60_1.value, &search60_2.value },
};
   
static const nir_search_variable replace60_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace60_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace60_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace60_0.value, &replace60_1.value, &replace60_2.value },
};
   
static const nir_search_variable search61_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search61_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search61_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search61_1_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search61_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search61_1_0.value, &search61_1_1.value, &search61_1_2.value },
};

static const nir_search_variable search61_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression search61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search61_0.value, &search61_1.value, &search61_2.value },
};
   
static const nir_search_variable replace61_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace61_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace61_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace61_0.value, &replace61_1.value, &replace61_2.value },
};
   
static const nir_search_variable search62_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search62_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_variable search62_2 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_bool32,
};
static const nir_search_expression search62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search62_0.value, &search62_1.value, &search62_2.value },
};
   
static const nir_search_variable replace62_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace62_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace62_0.value, &replace62_1.value },
};
   
static const nir_search_variable search152_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search152_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_constant search152_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search152_0.value, &search152_1.value, &search152_2.value },
};
   
static const nir_search_variable replace152_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace152_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace152_0.value, &replace152_1.value },
};
   
static const nir_search_variable search153_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search153_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

static const nir_search_constant search153_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search153_0.value, &search153_1.value, &search153_2.value },
};
   
static const nir_search_variable replace153_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace153_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace153_0.value, &replace153_1.value },
};
   
static const nir_search_constant search154_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_variable search154_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search154_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search154 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search154_0.value, &search154_1.value, &search154_2.value },
};
   
static const nir_search_variable replace154 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search155_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

static const nir_search_variable search155_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search155_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search155 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search155_0.value, &search155_1.value, &search155_2.value },
};
   
static const nir_search_variable replace155 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search156_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search156_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search156_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search156_0.value, &search156_1.value, &search156_2.value },
};
   
static const nir_search_variable replace156_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace156_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace156_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace156_0_0.value, &replace156_0_1.value },
};

static const nir_search_variable replace156_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace156_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace156_0.value, &replace156_1.value, &replace156_2.value },
};
   
static const nir_search_variable search157_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search157_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search157_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search157 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search157_0.value, &search157_1.value, &search157_2.value },
};
   
static const nir_search_variable replace157 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_bcsel_xforms[] = {
   { &search58, &replace58.value, 0 },
   { &search59, &replace59.value, 0 },
   { &search60, &replace60.value, 0 },
   { &search61, &replace61.value, 0 },
   { &search62, &replace62.value, 0 },
   { &search152, &replace152.value, 0 },
   { &search153, &replace153.value, 0 },
   { &search154, &replace154.value, 0 },
   { &search155, &replace155.value, 0 },
   { &search156, &replace156.value, 0 },
   { &search157, &replace157.value, 0 },
};
   
static const nir_search_variable search86_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search86_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search86_0.value, &search86_1.value },
};
   
static const nir_search_variable replace86_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace86_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace86_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace86_0_0.value, &replace86_0_1.value },
};
static const nir_search_expression replace86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace86_0.value },
};

static const struct transform nir_opt_algebraic_sge_xforms[] = {
   { &search86, &replace86.value, 10 },
};
   
static const nir_search_variable search134_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search134_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search134_0_0.value },
};
static const nir_search_expression search134 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsqrt,
   { &search134_0.value },
};
   
static const nir_search_constant replace134_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

static const nir_search_variable replace134_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace134_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace134_0_0.value, &replace134_0_1.value },
};
static const nir_search_expression replace134 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace134_0.value },
};
   
static const nir_search_variable search146_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search146 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search146_0.value },
};
   
static const nir_search_variable replace146_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace146_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace146_0_0.value },
};
static const nir_search_expression replace146 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace146_0.value },
};

static const struct transform nir_opt_algebraic_fsqrt_xforms[] = {
   { &search134, &replace134.value, 0 },
   { &search146, &replace146.value, 14 },
};
   
static const nir_search_variable search7_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search7_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search7 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search7_0.value, &search7_1.value },
};
   
static const nir_search_variable replace7 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search11_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search11_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search11_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search11_0_0.value, &search11_0_1.value },
};

static const nir_search_variable search11_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search11_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search11_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search11_1_0.value, &search11_1_1.value },
};
static const nir_search_expression search11 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search11_0.value, &search11_1.value },
};
   
static const nir_search_variable replace11_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace11_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace11_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace11_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace11_1_0.value, &replace11_1_1.value },
};
static const nir_search_expression replace11 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace11_0.value, &replace11_1.value },
};
   
static const nir_search_variable search13_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search13_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search13_0_0.value },
};

static const nir_search_variable search13_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search13 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search13_0.value, &search13_1.value },
};
   
static const nir_search_constant replace13 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search14_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search14_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search14_0_0.value },
};

static const nir_search_variable search14_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search14_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search14_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search14_1_0.value, &search14_1_1.value },
};
static const nir_search_expression search14 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search14_0.value, &search14_1.value },
};
   
static const nir_search_variable replace14 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search15_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search15_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search15_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search15_1_0_0.value },
};

static const nir_search_variable search15_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search15_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search15_1_0.value, &search15_1_1.value },
};
static const nir_search_expression search15 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search15_0.value, &search15_1.value },
};
   
static const nir_search_variable replace15 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search181_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search181_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search181_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search181_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search181_1_0.value, &search181_1_1.value },
};
static const nir_search_expression search181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search181_0.value, &search181_1.value },
};
   
static const nir_search_variable replace181_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace181_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace181_0.value, &replace181_1.value },
};
   
static const nir_search_variable search189_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search189_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search189_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search189_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search189_1_0.value, &search189_1_1.value },
};
static const nir_search_expression search189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search189_0.value, &search189_1.value },
};
   
static const nir_search_variable replace189_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace189_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace189_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace189_0_0.value, &replace189_0_1.value },
};

static const nir_search_variable replace189_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace189_0.value, &replace189_1.value },
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search7, &replace7.value, 0 },
   { &search11, &replace11.value, 0 },
   { &search13, &replace13.value, 0 },
   { &search14, &replace14.value, 0 },
   { &search15, &replace15.value, 0 },
   { &search181, &replace181.value, 0 },
   { &search189, &replace189.value, 0 },
};
   
static const nir_search_variable search202_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_2x16,
   { &search202_0.value },
};
   
static const nir_search_variable replace202_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace202_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace202_0_0_0_0_0.value },
};

static const nir_search_constant replace202_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
static const nir_search_expression replace202_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace202_0_0_0_0.value, &replace202_0_0_0_1.value },
};
static const nir_search_expression replace202_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace202_0_0_0.value },
};
static const nir_search_expression replace202_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace202_0_0.value },
};
static const nir_search_expression replace202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace202_0.value },
};

static const struct transform nir_opt_algebraic_pack_unorm_2x16_xforms[] = {
   { &search202, &replace202.value, 28 },
};
   
static const nir_search_variable search203_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_4x8,
   { &search203_0.value },
};
   
static const nir_search_variable replace203_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace203_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace203_0_0_0_0_0.value },
};

static const nir_search_constant replace203_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
static const nir_search_expression replace203_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace203_0_0_0_0.value, &replace203_0_0_0_1.value },
};
static const nir_search_expression replace203_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace203_0_0_0.value },
};
static const nir_search_expression replace203_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace203_0_0.value },
};
static const nir_search_expression replace203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace203_0.value },
};

static const struct transform nir_opt_algebraic_pack_unorm_4x8_xforms[] = {
   { &search203, &replace203.value, 29 },
};
   
static const nir_search_variable search205_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_4x8,
   { &search205_0.value },
};
   
static const nir_search_constant replace205_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace205_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace205_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace205_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace205_0_0_0_0_1_0.value, &replace205_0_0_0_0_1_1.value },
};
static const nir_search_expression replace205_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace205_0_0_0_0_0.value, &replace205_0_0_0_0_1.value },
};

static const nir_search_constant replace205_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
static const nir_search_expression replace205_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace205_0_0_0_0.value, &replace205_0_0_0_1.value },
};
static const nir_search_expression replace205_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace205_0_0_0.value },
};
static const nir_search_expression replace205_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace205_0_0.value },
};
static const nir_search_expression replace205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace205_0.value },
};

static const struct transform nir_opt_algebraic_pack_snorm_4x8_xforms[] = {
   { &search205, &replace205.value, 31 },
};
   
static const nir_search_variable search103_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search103_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fand,
   { &search103_0.value, &search103_1.value },
};
   
static const nir_search_constant replace103 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search103, &replace103.value, 0 },
};
   
static const nir_search_variable search2_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search2_0_0.value },
};
static const nir_search_expression search2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search2_0.value },
};
   
static const nir_search_variable replace2_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace2_0.value },
};
   
static const nir_search_variable search3_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search3_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search3_0_0.value },
};
static const nir_search_expression search3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search3_0.value },
};
   
static const nir_search_variable replace3_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace3_0.value },
};
   
static const nir_search_variable search81_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search81_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search81_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search81_0_0.value, &search81_0_1.value },
};
static const nir_search_expression search81 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search81_0.value },
};
   
static const nir_search_variable replace81_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace81_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace81 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &replace81_0.value, &replace81_1.value },
};
   
static const nir_search_variable search82_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search82_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search82_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search82_0_0.value, &search82_0_1.value },
};
static const nir_search_expression search82 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search82_0.value },
};
   
static const nir_search_variable replace82_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace82_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace82 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &replace82_0.value, &replace82_1.value },
};
   
static const nir_search_variable search83_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search83_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search83_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search83_0_0.value, &search83_0_1.value },
};
static const nir_search_expression search83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search83_0.value },
};
   
static const nir_search_variable replace83_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace83_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &replace83_0.value, &replace83_1.value },
};
   
static const nir_search_variable search84_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search84_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search84_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search84_0_0.value, &search84_0_1.value },
};
static const nir_search_expression search84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search84_0.value },
};
   
static const nir_search_variable replace84_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace84_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &replace84_0.value, &replace84_1.value },
};
   
static const nir_search_variable search164_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search164_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search164_0_0.value },
};
static const nir_search_expression search164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search164_0.value },
};
   
static const nir_search_variable replace164_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace164_0.value },
};
   
static const nir_search_constant search182_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search182_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search182_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search182_0_0.value, &search182_0_1.value },
};
static const nir_search_expression search182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search182_0.value },
};
   
static const nir_search_variable replace182_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace182_0.value },
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search2, &replace2.value, 0 },
   { &search3, &replace3.value, 0 },
   { &search81, &replace81.value, 0 },
   { &search82, &replace82.value, 0 },
   { &search83, &replace83.value, 0 },
   { &search84, &replace84.value, 0 },
   { &search164, &replace164.value, 0 },
   { &search182, &replace182.value, 0 },
};
   
static const nir_search_variable search99_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search99_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search99_0.value, &search99_1.value },
};
   
static const nir_search_constant replace99 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search148_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
};

static const nir_search_constant search148_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search148 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search148_0.value, &search148_1.value },
};
   
static const nir_search_variable replace148 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search151_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
};

static const nir_search_constant search151_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search151 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search151_0.value, &search151_1.value },
};
   
static const nir_search_variable replace151_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace151 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace151_0.value },
};
   
static const nir_search_variable search225_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search225_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search225_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search225_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search225_0_0.value, &search225_0_1.value, &search225_0_2.value },
};

static const nir_search_variable search225_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search225_0.value, &search225_1.value },
};
   
static const nir_search_variable replace225_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace225_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace225_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace225_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace225_1_0.value, &replace225_1_1.value },
};

static const nir_search_variable replace225_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace225_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace225_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace225_2_0.value, &replace225_2_1.value },
};
static const nir_search_expression replace225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace225_0.value, &replace225_1.value, &replace225_2.value },
};
   
static const nir_search_variable search226_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search226_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search226_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search226_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search226_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search226_1_0.value, &search226_1_1.value, &search226_1_2.value },
};
static const nir_search_expression search226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search226_0.value, &search226_1.value },
};
   
static const nir_search_variable replace226_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace226_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace226_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace226_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace226_1_0.value, &replace226_1_1.value },
};

static const nir_search_variable replace226_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace226_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace226_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace226_2_0.value, &replace226_2_1.value },
};
static const nir_search_expression replace226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace226_0.value, &replace226_1.value, &replace226_2.value },
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search99, &replace99.value, 0 },
   { &search148, &replace148.value, 0 },
   { &search151, &replace151.value, 0 },
   { &search225, &replace225.value, 0 },
   { &search226, &replace226.value, 0 },
};
   
static const nir_search_variable search65_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search65_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search65_0.value, &search65_1.value },
};
   
static const nir_search_variable replace65 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search74_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search74_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search74_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search74_0_0_0_0.value, &search74_0_0_0_1.value },
};

static const nir_search_variable search74_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search74_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search74_0_0_0.value, &search74_0_0_1.value },
};

static const nir_search_variable search74_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search74_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search74_0_0.value, &search74_0_1.value },
};

static const nir_search_variable search74_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search74_0.value, &search74_1.value },
};
   
static const nir_search_variable replace74_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace74_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace74_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace74_0_0.value, &replace74_0_1.value },
};

static const nir_search_variable replace74_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace74_0.value, &replace74_1.value },
};

static const struct transform nir_opt_algebraic_imin_xforms[] = {
   { &search65, &replace65.value, 0 },
   { &search74, &replace74.value, 0 },
};
   
static const nir_search_variable search136_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search136_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search136_0_0.value },
};
static const nir_search_expression search136 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frsq,
   { &search136_0.value },
};
   
static const nir_search_constant replace136_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

static const nir_search_variable replace136_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace136_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace136_0_0.value, &replace136_0_1.value },
};
static const nir_search_expression replace136 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace136_0.value },
};

static const struct transform nir_opt_algebraic_frsq_xforms[] = {
   { &search136, &replace136.value, 0 },
};
   
static const nir_search_variable search1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search1_0_0.value },
};
static const nir_search_expression search1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search1_0.value },
};
   
static const nir_search_variable replace1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search179_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search179_0.value },
};
   
static const nir_search_constant replace179_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace179_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace179_0.value, &replace179_1.value },
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search1, &replace1.value, 0 },
   { &search179, &replace179.value, 19 },
};
   
static const nir_search_variable search76_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search76_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search76_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search76_0_0_0.value, &search76_0_0_1.value },
};

static const nir_search_constant search76_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression search76_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search76_0_0.value, &search76_0_1.value },
};

static const nir_search_constant search76_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search76_0.value, &search76_1.value },
};
   
static const nir_search_variable replace76_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace76_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace76_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace76_0_0.value, &replace76_0_1.value },
};

static const nir_search_constant replace76_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression replace76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace76_0.value, &replace76_1.value },
};
   
static const nir_search_variable search199_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search199_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search199_0.value, &search199_1.value },
};
   
static const nir_search_variable replace199_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace199_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace199_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression replace199_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace199_0_1_0.value, &replace199_0_1_1.value },
};
static const nir_search_expression replace199_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace199_0_0.value, &replace199_0_1.value },
};

static const nir_search_constant replace199_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression replace199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace199_0.value, &replace199_1.value },
};

static const struct transform nir_opt_algebraic_extract_u8_xforms[] = {
   { &search76, &replace76.value, 0 },
   { &search199, &replace199.value, 26 },
};
   
static const nir_search_variable search204_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_2x16,
   { &search204_0.value },
};
   
static const nir_search_constant replace204_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace204_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace204_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace204_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace204_0_0_0_0_1_0.value, &replace204_0_0_0_0_1_1.value },
};
static const nir_search_expression replace204_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace204_0_0_0_0_0.value, &replace204_0_0_0_0_1.value },
};

static const nir_search_constant replace204_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
static const nir_search_expression replace204_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace204_0_0_0_0.value, &replace204_0_0_0_1.value },
};
static const nir_search_expression replace204_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace204_0_0_0.value },
};
static const nir_search_expression replace204_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace204_0_0.value },
};
static const nir_search_expression replace204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace204_0.value },
};

static const struct transform nir_opt_algebraic_pack_snorm_2x16_xforms[] = {
   { &search204, &replace204.value, 30 },
};
   
static const nir_search_variable search125_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search125_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search125 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search125_0.value, &search125_1.value },
};
   
static const nir_search_variable replace125_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace125_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace125_0_0_0.value },
};

static const nir_search_variable replace125_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace125_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace125_0_0.value, &replace125_0_1.value },
};
static const nir_search_expression replace125 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace125_0.value },
};
   
static const nir_search_variable search128_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search128_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search128 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search128_0.value, &search128_1.value },
};
   
static const nir_search_variable replace128 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search129_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search129_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};
static const nir_search_expression search129 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search129_0.value, &search129_1.value },
};
   
static const nir_search_variable replace129_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace129_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace129 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace129_0.value, &replace129_1.value },
};
   
static const nir_search_variable search130_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search130_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4010000000000000 /* 4.0 */ },
};
static const nir_search_expression search130 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search130_0.value, &search130_1.value },
};
   
static const nir_search_variable replace130_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace130_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace130_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace130_0_0.value, &replace130_0_1.value },
};

static const nir_search_variable replace130_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace130_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace130_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace130_1_0.value, &replace130_1_1.value },
};
static const nir_search_expression replace130 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace130_0.value, &replace130_1.value },
};
   
static const nir_search_constant search131_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};

static const nir_search_variable search131_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search131 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search131_0.value, &search131_1.value },
};
   
static const nir_search_variable replace131_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace131_0.value },
};
   
static const nir_search_variable search132_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search132_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
static const nir_search_expression search132_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search132_0_0.value, &search132_0_1.value },
};

static const nir_search_constant search132_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
static const nir_search_expression search132 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search132_0.value, &search132_1.value },
};
   
static const nir_search_variable replace132 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search133_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search133_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
static const nir_search_expression search133_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search133_0_0_0.value, &search133_0_0_1.value },
};
static const nir_search_expression search133_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search133_0_0.value },
};

static const nir_search_constant search133_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
static const nir_search_expression search133 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search133_0.value, &search133_1.value },
};
   
static const nir_search_variable replace133_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace133 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace133_0.value },
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search125, &replace125.value, 11 },
   { &search128, &replace128.value, 0 },
   { &search129, &replace129.value, 0 },
   { &search130, &replace130.value, 0 },
   { &search131, &replace131.value, 0 },
   { &search132, &replace132.value, 0 },
   { &search133, &replace133.value, 0 },
};
   
static const nir_search_variable search98_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search98_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search98_0.value, &search98_1.value },
};
   
static const nir_search_constant replace98 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search223_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search223_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search223_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search223_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search223_0_0.value, &search223_0_1.value, &search223_0_2.value },
};

static const nir_search_variable search223_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search223_0.value, &search223_1.value },
};
   
static const nir_search_variable replace223_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace223_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace223_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace223_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace223_1_0.value, &replace223_1_1.value },
};

static const nir_search_variable replace223_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace223_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace223_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace223_2_0.value, &replace223_2_1.value },
};
static const nir_search_expression replace223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace223_0.value, &replace223_1.value, &replace223_2.value },
};
   
static const nir_search_variable search224_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search224_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search224_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search224_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search224_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search224_1_0.value, &search224_1_1.value, &search224_1_2.value },
};
static const nir_search_expression search224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search224_0.value, &search224_1.value },
};
   
static const nir_search_variable replace224_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace224_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace224_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace224_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace224_1_0.value, &replace224_1_1.value },
};

static const nir_search_variable replace224_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace224_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace224_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace224_2_0.value, &replace224_2_1.value },
};
static const nir_search_expression replace224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace224_0.value, &replace224_1.value, &replace224_2.value },
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search98, &replace98.value, 0 },
   { &search223, &replace223.value, 0 },
   { &search224, &replace224.value, 0 },
};
   
static const nir_search_constant search142_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search142_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search142 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fdiv,
   { &search142_0.value, &search142_1.value },
};
   
static const nir_search_variable replace142_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace142 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace142_0.value },
};
   
static const nir_search_variable search143_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search143_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search143 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &search143_0.value, &search143_1.value },
};
   
static const nir_search_variable replace143_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace143_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace143_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace143_1_0.value },
};
static const nir_search_expression replace143 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace143_0.value, &replace143_1.value },
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search142, &replace142.value, 0 },
   { &search143, &replace143.value, 13 },
};
   
static const nir_search_variable search38_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search38 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffract,
   { &search38_0.value },
};
   
static const nir_search_variable replace38_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace38_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace38_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace38_1_0.value },
};
static const nir_search_expression replace38 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace38_0.value, &replace38_1.value },
};

static const struct transform nir_opt_algebraic_ffract_xforms[] = {
   { &search38, &replace38.value, 3 },
};
   
static const nir_search_variable search6_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search6_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search6 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search6_0.value, &search6_1.value },
};
   
static const nir_search_variable replace6 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search10_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search10_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search10_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search10_0_0.value, &search10_0_1.value },
};

static const nir_search_variable search10_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search10_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search10_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search10_1_0.value, &search10_1_1.value },
};
static const nir_search_expression search10 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search10_0.value, &search10_1.value },
};
   
static const nir_search_variable replace10_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace10_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace10_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace10_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace10_1_0.value, &replace10_1_1.value },
};
static const nir_search_expression replace10 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace10_0.value, &replace10_1.value },
};
   
static const nir_search_variable search12_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search12_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search12_0_0.value },
};

static const nir_search_variable search12_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search12 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search12_0.value, &search12_1.value },
};
   
static const nir_search_constant replace12 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search16_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search16_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search16_0_0.value },
};

static const nir_search_variable search16_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search16_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search16_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search16_1_0.value, &search16_1_1.value },
};
static const nir_search_expression search16 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search16_0.value, &search16_1.value },
};
   
static const nir_search_variable replace16 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search17_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search17_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search17_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search17_1_0_0.value },
};

static const nir_search_variable search17_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search17_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search17_1_0.value, &search17_1_1.value },
};
static const nir_search_expression search17 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search17_0.value, &search17_1.value },
};
   
static const nir_search_variable replace17 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search39_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search39_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search39_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search39_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search39_0_1_1_0_0.value },
};
static const nir_search_expression search39_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search39_0_1_1_0.value },
};
static const nir_search_expression search39_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search39_0_1_0.value, &search39_0_1_1.value },
};
static const nir_search_expression search39_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search39_0_0.value, &search39_0_1.value },
};

static const nir_search_variable search39_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search39_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search39_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search39_1_1_0.value },
};
static const nir_search_expression search39_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search39_1_0.value, &search39_1_1.value },
};
static const nir_search_expression search39 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search39_0.value, &search39_1.value },
};
   
static const nir_search_variable replace39_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace39_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace39_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace39 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace39_0.value, &replace39_1.value, &replace39_2.value },
};
   
static const nir_search_variable search40_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search40_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search40_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search40_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search40_0_1_1_0.value },
};
static const nir_search_expression search40_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search40_0_1_0.value, &search40_0_1_1.value },
};
static const nir_search_expression search40_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search40_0_0.value, &search40_0_1.value },
};

static const nir_search_variable search40_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search40_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search40_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search40_1_0.value, &search40_1_1.value },
};
static const nir_search_expression search40 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search40_0.value, &search40_1.value },
};
   
static const nir_search_variable replace40_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace40_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace40_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace40 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace40_0.value, &replace40_1.value, &replace40_2.value },
};
   
static const nir_search_variable search41_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search41_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search41_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search41_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search41_0_1_1_0.value },
};
static const nir_search_expression search41_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search41_0_1_0.value, &search41_0_1_1.value },
};
static const nir_search_expression search41_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search41_0_0.value, &search41_0_1.value },
};

static const nir_search_variable search41_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search41_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search41_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search41_1_0.value, &search41_1_1.value },
};
static const nir_search_expression search41 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search41_0.value, &search41_1.value },
};
   
static const nir_search_variable replace41_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace41_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace41_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace41 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace41_0.value, &replace41_1.value, &replace41_2.value },
};
   
static const nir_search_variable search42_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search42_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search42_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search42_1_0_0.value },
};

static const nir_search_variable search42_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search42_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search42_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search42_1_1_1_0.value },
};
static const nir_search_expression search42_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search42_1_1_0.value, &search42_1_1_1.value },
};
static const nir_search_expression search42_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search42_1_0.value, &search42_1_1.value },
};
static const nir_search_expression search42 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search42_0.value, &search42_1.value },
};
   
static const nir_search_variable replace42_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace42_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace42_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace42 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace42_0.value, &replace42_1.value, &replace42_2.value },
};
   
static const nir_search_variable search43_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search43_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable search43_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search43_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search43_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search43_1_1_1_0.value },
};
static const nir_search_expression search43_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search43_1_1_0.value, &search43_1_1_1.value },
};
static const nir_search_expression search43_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search43_1_0.value, &search43_1_1.value },
};
static const nir_search_expression search43 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search43_0.value, &search43_1.value },
};
   
static const nir_search_variable replace43_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace43_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace43_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace43 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace43_0.value, &replace43_1.value, &replace43_2.value },
};
   
static const nir_search_variable search44_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search44_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable search44_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search44_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search44_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search44_1_1_1_0.value },
};
static const nir_search_expression search44_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search44_1_1_0.value, &search44_1_1_1.value },
};
static const nir_search_expression search44_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search44_1_0.value, &search44_1_1.value },
};
static const nir_search_expression search44 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search44_0.value, &search44_1.value },
};
   
static const nir_search_variable replace44_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace44_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace44_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace44 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace44_0.value, &replace44_1.value, &replace44_2.value },
};
   
static const nir_search_variable search46_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search46_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search46_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search46_0_0.value, &search46_0_1.value },
};

static const nir_search_variable search46_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search46 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search46_0.value, &search46_1.value },
};
   
static const nir_search_variable replace46_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace46_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace46_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace46 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &replace46_0.value, &replace46_1.value, &replace46_2.value },
};
   
static const nir_search_variable search180_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search180_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search180_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search180_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search180_1_0.value, &search180_1_1.value },
};
static const nir_search_expression search180 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search180_0.value, &search180_1.value },
};
   
static const nir_search_variable replace180_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace180_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace180_0.value, &replace180_1.value },
};
   
static const nir_search_variable search188_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search188_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search188_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search188_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search188_1_0.value, &search188_1_1.value },
};
static const nir_search_expression search188 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search188_0.value, &search188_1.value },
};
   
static const nir_search_variable replace188_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace188_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace188_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace188_0_0.value, &replace188_0_1.value },
};

static const nir_search_variable replace188_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace188 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace188_0.value, &replace188_1.value },
};

static const struct transform nir_opt_algebraic_fadd_xforms[] = {
   { &search6, &replace6.value, 0 },
   { &search10, &replace10.value, 0 },
   { &search12, &replace12.value, 0 },
   { &search16, &replace16.value, 0 },
   { &search17, &replace17.value, 0 },
   { &search39, &replace39.value, 1 },
   { &search40, &replace40.value, 4 },
   { &search41, &replace41.value, 5 },
   { &search42, &replace42.value, 1 },
   { &search43, &replace43.value, 4 },
   { &search44, &replace44.value, 5 },
   { &search46, &replace46.value, 7 },
   { &search180, &replace180.value, 0 },
   { &search188, &replace188.value, 0 },
};
   
static const nir_search_constant search115_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search115_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search115_0.value, &search115_1.value },
};
   
static const nir_search_constant replace115 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search116_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search116_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search116 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search116_0.value, &search116_1.value },
};
   
static const nir_search_variable replace116 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search115, &replace115.value, 0 },
   { &search116, &replace116.value, 0 },
};
   
static const nir_search_variable search194_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search194_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search194 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_usub_borrow,
   { &search194_0.value, &search194_1.value },
};
   
static const nir_search_variable replace194_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace194_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace194_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace194_0_0.value, &replace194_0_1.value },
};
static const nir_search_expression replace194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace194_0.value },
};

static const struct transform nir_opt_algebraic_usub_borrow_xforms[] = {
   { &search194, &replace194.value, 23 },
};
   
static const nir_search_variable search124_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search124_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search124_0_0.value },
};
static const nir_search_expression search124 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search124_0.value },
};
   
static const nir_search_variable replace124 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search137_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search137_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search137_0_0.value },
};
static const nir_search_expression search137 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search137_0.value },
};
   
static const nir_search_constant replace137_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

static const nir_search_variable replace137_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace137_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace137_1_0.value },
};
static const nir_search_expression replace137 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace137_0.value, &replace137_1.value },
};
   
static const nir_search_variable search138_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search138_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search138_0_0.value },
};
static const nir_search_expression search138 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search138_0.value },
};
   
static const nir_search_variable replace138_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace138_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace138_0_0.value },
};
static const nir_search_expression replace138 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace138_0.value },
};
   
static const nir_search_variable search139_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search139_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search139_0_0.value },
};
static const nir_search_expression search139 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search139_0.value },
};
   
static const nir_search_constant replace139_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

static const nir_search_variable replace139_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace139_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace139_1_0.value },
};
static const nir_search_expression replace139 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace139_0.value, &replace139_1.value },
};
   
static const nir_search_variable search140_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search140_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search140_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search140_0_0.value, &search140_0_1.value },
};
static const nir_search_expression search140 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search140_0.value },
};
   
static const nir_search_variable replace140_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace140_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace140_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace140_1_0.value },
};
static const nir_search_expression replace140 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace140_0.value, &replace140_1.value },
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search124, &replace124.value, 0 },
   { &search137, &replace137.value, 0 },
   { &search138, &replace138.value, 0 },
   { &search139, &replace139.value, 0 },
   { &search140, &replace140.value, 0 },
};
   
static const nir_search_variable search47_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search47_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search47_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search47_0_0.value, &search47_0_1.value },
};
static const nir_search_expression search47 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search47_0.value },
};
   
static const nir_search_variable replace47_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace47_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace47 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace47_0.value, &replace47_1.value },
};
   
static const nir_search_variable search48_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search48_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search48_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search48_0_0.value, &search48_0_1.value },
};
static const nir_search_expression search48 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search48_0.value },
};
   
static const nir_search_variable replace48_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace48_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace48 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace48_0.value, &replace48_1.value },
};
   
static const nir_search_variable search49_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search49_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search49_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search49_0_0.value, &search49_0_1.value },
};
static const nir_search_expression search49 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search49_0.value },
};
   
static const nir_search_variable replace49_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace49_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace49 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace49_0.value, &replace49_1.value },
};
   
static const nir_search_variable search50_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search50_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search50_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search50_0_0.value, &search50_0_1.value },
};
static const nir_search_expression search50 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search50_0.value },
};
   
static const nir_search_variable replace50_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace50_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace50 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace50_0.value, &replace50_1.value },
};
   
static const nir_search_variable search51_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search51_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search51_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search51_0_0.value, &search51_0_1.value },
};
static const nir_search_expression search51 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search51_0.value },
};
   
static const nir_search_variable replace51_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace51_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace51 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace51_0.value, &replace51_1.value },
};
   
static const nir_search_variable search52_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search52_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search52_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search52_0_0.value, &search52_0_1.value },
};
static const nir_search_expression search52 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search52_0.value },
};
   
static const nir_search_variable replace52_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace52_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace52 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace52_0.value, &replace52_1.value },
};
   
static const nir_search_variable search53_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search53_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search53_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search53_0_0.value, &search53_0_1.value },
};
static const nir_search_expression search53 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search53_0.value },
};
   
static const nir_search_variable replace53_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace53_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace53 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace53_0.value, &replace53_1.value },
};
   
static const nir_search_variable search54_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search54_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search54_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search54_0_0.value, &search54_0_1.value },
};
static const nir_search_expression search54 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search54_0.value },
};
   
static const nir_search_variable replace54_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace54_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace54 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace54_0.value, &replace54_1.value },
};
   
static const nir_search_variable search112_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search112_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search112_0_0.value },
};
static const nir_search_expression search112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search112_0.value },
};
   
static const nir_search_variable replace112 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_inot_xforms[] = {
   { &search47, &replace47.value, 0 },
   { &search48, &replace48.value, 0 },
   { &search49, &replace49.value, 0 },
   { &search50, &replace50.value, 0 },
   { &search51, &replace51.value, 0 },
   { &search52, &replace52.value, 0 },
   { &search53, &replace53.value, 0 },
   { &search54, &replace54.value, 0 },
   { &search112, &replace112.value, 0 },
};
   
static const nir_search_variable search88_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search88_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search88_0.value, &search88_1.value },
};
   
static const nir_search_variable replace88_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace88_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace88_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace88_0_0.value, &replace88_0_1.value },
};
static const nir_search_expression replace88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace88_0.value },
};

static const struct transform nir_opt_algebraic_sne_xforms[] = {
   { &search88, &replace88.value, 10 },
};
   
static const nir_search_variable search190_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search190_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search190 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_fmod,
   { &search190_0.value, &search190_1.value },
};
   
static const nir_search_variable replace190_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace190_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace190_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace190_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace190_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace190_1_1_0_0.value, &replace190_1_1_0_1.value },
};
static const nir_search_expression replace190_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace190_1_1_0.value },
};
static const nir_search_expression replace190_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace190_1_0.value, &replace190_1_1.value },
};
static const nir_search_expression replace190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace190_0.value, &replace190_1.value },
};
   
static const nir_search_variable search191_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search191_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search191 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_fmod,
   { &search191_0.value, &search191_1.value },
};
   
static const nir_search_variable replace191_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace191_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace191_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace191_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace191_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace191_1_1_0_0.value, &replace191_1_1_0_1.value },
};
static const nir_search_expression replace191_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace191_1_1_0.value },
};
static const nir_search_expression replace191_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace191_1_0.value, &replace191_1_1.value },
};
static const nir_search_expression replace191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace191_0.value, &replace191_1.value },
};

static const struct transform nir_opt_algebraic_fmod_xforms[] = {
   { &search190, &replace190.value, 20 },
   { &search191, &replace191.value, 21 },
};
   
static const nir_search_variable search161_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search161_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search161_0_0.value },
};
static const nir_search_expression search161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &search161_0.value },
};
   
static const nir_search_variable replace161_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u,
   { &replace161_0.value },
};

static const struct transform nir_opt_algebraic_f2u_xforms[] = {
   { &search161, &replace161.value, 0 },
};
   
static const nir_search_variable search158_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search158_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search158_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search158 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fcsel,
   { &search158_0.value, &search158_1.value, &search158_2.value },
};
   
static const nir_search_variable replace158 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_fcsel_xforms[] = {
   { &search158, &replace158.value, 0 },
};
   
static const nir_search_variable search173_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search173_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search173_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search173_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search173_1_0.value, &search173_1_1.value },
};
static const nir_search_expression search173 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search173_0.value, &search173_1.value },
};
   
static const nir_search_variable replace173_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace173_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace173 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace173_0.value, &replace173_1.value },
};
   
static const nir_search_variable search177_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search177_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search177_0.value, &search177_1.value },
};
   
static const nir_search_variable replace177_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace177_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace177_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace177_1_0.value },
};
static const nir_search_expression replace177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace177_0.value, &replace177_1.value },
};

static const struct transform nir_opt_algebraic_isub_xforms[] = {
   { &search173, &replace173.value, 0 },
   { &search177, &replace177.value, 18 },
};
   
static const nir_search_variable search64_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search64_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search64_0.value, &search64_1.value },
};
   
static const nir_search_variable replace64 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search70_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search70_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search70_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search70_0_0.value, &search70_0_1.value },
};

static const nir_search_constant search70_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search70 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmax,
   { &search70_0.value, &search70_1.value },
};
   
static const nir_search_variable replace70_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace70_0.value },
};

static const struct transform nir_opt_algebraic_fmax_xforms[] = {
   { &search64, &replace64.value, 0 },
   { &search70, &replace70.value, 8 },
};
   
static const nir_search_variable search20_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search20_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search20 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search20_0.value, &search20_1.value },
};
   
static const nir_search_constant replace20 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search21_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search21_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search21 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search21_0.value, &search21_1.value },
};
   
static const nir_search_variable replace21 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_umul_unorm_4x8_xforms[] = {
   { &search20, &replace20.value, 0 },
   { &search21, &replace21.value, 0 },
};
   
static const nir_search_variable search195_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
};

static const nir_search_variable search195_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
};

static const nir_search_variable search195_2 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
};

static const nir_search_variable search195_3 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression search195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_insert,
   { &search195_0.value, &search195_1.value, &search195_2.value, &search195_3.value },
};
   
static const nir_search_constant replace195_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace195_0_1 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace195_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace195_0_0.value, &replace195_0_1.value },
};

static const nir_search_variable replace195_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace195_2_0_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace195_2_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace195_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace195_2_0_0.value, &replace195_2_0_1.value },
};

static const nir_search_variable replace195_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace195_2_2 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace195_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfi,
   { &replace195_2_0.value, &replace195_2_1.value, &replace195_2_2.value },
};
static const nir_search_expression replace195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace195_0.value, &replace195_1.value, &replace195_2.value },
};

static const struct transform nir_opt_algebraic_bitfield_insert_xforms[] = {
   { &search195, &replace195.value, 24 },
};
   
static const nir_search_variable search90_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search90_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search90_0_0.value },
};

static const nir_search_variable search90_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search90_0.value, &search90_1.value },
};
   
static const nir_search_variable replace90_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace90_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace90_0.value, &replace90_1.value },
};
   
static const nir_search_variable search217_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search217_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search217_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search217_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search217_0_0.value, &search217_0_1.value, &search217_0_2.value },
};

static const nir_search_variable search217_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search217_0.value, &search217_1.value },
};
   
static const nir_search_variable replace217_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace217_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace217_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace217_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace217_1_0.value, &replace217_1_1.value },
};

static const nir_search_variable replace217_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace217_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace217_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace217_2_0.value, &replace217_2_1.value },
};
static const nir_search_expression replace217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace217_0.value, &replace217_1.value, &replace217_2.value },
};
   
static const nir_search_variable search218_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search218_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search218_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search218_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search218_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search218_1_0.value, &search218_1_1.value, &search218_1_2.value },
};
static const nir_search_expression search218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search218_0.value, &search218_1.value },
};
   
static const nir_search_variable replace218_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace218_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace218_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace218_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace218_1_0.value, &replace218_1_1.value },
};

static const nir_search_variable replace218_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace218_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace218_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace218_2_0.value, &replace218_2_1.value },
};
static const nir_search_expression replace218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace218_0.value, &replace218_1.value, &replace218_2.value },
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search90, &replace90.value, 0 },
   { &search217, &replace217.value, 0 },
   { &search218, &replace218.value, 0 },
};
   
static const nir_search_variable search31_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search31_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search31_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search31 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search31_0.value, &search31_1.value, &search31_2.value },
};
   
static const nir_search_variable replace31 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search32_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search32_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search32_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search32 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search32_0.value, &search32_1.value, &search32_2.value },
};
   
static const nir_search_variable replace32 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search33_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search33_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search33_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search33 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search33_0.value, &search33_1.value, &search33_2.value },
};
   
static const nir_search_variable replace33 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search34_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search34_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search34_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search34 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search34_0.value, &search34_1.value, &search34_2.value },
};
   
static const nir_search_variable replace34_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace34_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace34 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace34_0.value, &replace34_1.value },
};
   
static const nir_search_variable search35_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search35_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search35_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search35_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search35_2_0.value },
};
static const nir_search_expression search35 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search35_0.value, &search35_1.value, &search35_2.value },
};
   
static const nir_search_variable replace35_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace35_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace35_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace35 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace35_0.value, &replace35_1.value, &replace35_2.value },
};
   
static const nir_search_variable search36_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search36_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search36_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search36 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_flrp,
   { &search36_0.value, &search36_1.value, &search36_2.value },
};
   
static const nir_search_variable replace36_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace36_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace36_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace36_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace36_0_1_0.value, &replace36_0_1_1.value },
};
static const nir_search_expression replace36_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace36_0_0.value, &replace36_0_1.value },
};

static const nir_search_variable replace36_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace36_0.value, &replace36_1.value },
};
   
static const nir_search_variable search37_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search37_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search37_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search37 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_flrp,
   { &search37_0.value, &search37_1.value, &search37_2.value },
};
   
static const nir_search_variable replace37_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace37_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace37_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace37_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace37_0_1_0.value, &replace37_0_1_1.value },
};
static const nir_search_expression replace37_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace37_0_0.value, &replace37_0_1.value },
};

static const nir_search_variable replace37_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace37 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace37_0.value, &replace37_1.value },
};

static const struct transform nir_opt_algebraic_flrp_xforms[] = {
   { &search31, &replace31.value, 0 },
   { &search32, &replace32.value, 0 },
   { &search33, &replace33.value, 0 },
   { &search34, &replace34.value, 0 },
   { &search35, &replace35.value, 1 },
   { &search36, &replace36.value, 1 },
   { &search37, &replace37.value, 2 },
};
   
static const nir_search_variable search77_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search77_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search77_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search77_0_0.value, &search77_0_1.value },
};

static const nir_search_variable search77_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search77_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search77_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search77_1_0.value, &search77_1_1.value },
};
static const nir_search_expression search77 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search77_0.value, &search77_1.value },
};
   
static const nir_search_variable replace77_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace77_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace77_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace77_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace77_1_0.value, &replace77_1_1.value },
};
static const nir_search_expression replace77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace77_0.value, &replace77_1.value },
};
   
static const nir_search_variable search78_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search78_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search78_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search78_0_0.value, &search78_0_1.value },
};

static const nir_search_variable search78_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search78_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search78_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search78_1_0.value, &search78_1_1.value },
};
static const nir_search_expression search78 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search78_0.value, &search78_1.value },
};
   
static const nir_search_variable replace78_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace78_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace78_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace78_0_0.value, &replace78_0_1.value },
};

static const nir_search_variable replace78_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace78 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace78_0.value, &replace78_1.value },
};
   
static const nir_search_variable search79_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search79_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search79_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search79_0_0.value, &search79_0_1.value },
};

static const nir_search_variable search79_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search79_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search79_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search79_1_0.value, &search79_1_1.value },
};
static const nir_search_expression search79 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search79_0.value, &search79_1.value },
};
   
static const nir_search_variable replace79_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace79_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace79_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace79_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace79_1_0.value, &replace79_1_1.value },
};
static const nir_search_expression replace79 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace79_0.value, &replace79_1.value },
};
   
static const nir_search_variable search80_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search80_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search80_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search80_0_0.value, &search80_0_1.value },
};

static const nir_search_variable search80_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search80_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search80_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search80_1_0.value, &search80_1_1.value },
};
static const nir_search_expression search80 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search80_0.value, &search80_1.value },
};
   
static const nir_search_variable replace80_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace80_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace80_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace80_0_0.value, &replace80_0_1.value },
};

static const nir_search_variable replace80_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace80 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace80_0.value, &replace80_1.value },
};
   
static const nir_search_variable search107_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search107_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search107 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search107_0.value, &search107_1.value },
};
   
static const nir_search_variable replace107 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search108_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search108_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search108 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search108_0.value, &search108_1.value },
};
   
static const nir_search_variable replace108 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search114_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search114_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search114_0_0.value },
};

static const nir_search_variable search114_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search114_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search114_1_0.value },
};
static const nir_search_expression search114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search114_0.value, &search114_1.value },
};
   
static const nir_search_variable replace114_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace114_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace114_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace114_0_0.value, &replace114_0_1.value },
};
static const nir_search_expression replace114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace114_0.value },
};
   
static const nir_search_variable search212_0_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search212_0_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search212_0_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_0_0_0_1_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0_0_0_0_1_0_0.value, &search212_0_0_0_0_0_0_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_0_0_0_1_0.value, &search212_0_0_0_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search212_0_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_1_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0_1_0_0_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_1_0_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search212_0_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_1_0_0_1_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0_1_0_0_1_0_0.value, &search212_0_0_0_0_0_0_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_1_0_0_1_0.value, &search212_0_0_0_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_1_0_0_0.value, &search212_0_0_0_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0_1_0_0.value, &search212_0_0_0_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_0_0_0_1_0.value, &search212_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0_0_0_0.value, &search212_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
static const nir_search_expression search212_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_0_0_0.value, &search212_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search212_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_0_0.value, &search212_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search212_0_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_0_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search212_0_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_0_0_0_1_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0_0_0_0_1_0_0.value, &search212_0_0_0_1_0_0_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_0_0_0_1_0.value, &search212_0_0_0_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0_0_0_0.value, &search212_0_0_0_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_0_0.value, &search212_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search212_0_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_1_0_0_0_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0_1_0_0_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_1_0_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_0_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search212_0_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_1_0_0_1_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0_1_0_0_1_0_0.value, &search212_0_0_0_1_0_0_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_1_0_0_1_0.value, &search212_0_0_0_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_1_0_0_0.value, &search212_0_0_0_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0_1_0_0.value, &search212_0_0_0_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0_0_1_0.value, &search212_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_1_0_0_0.value, &search212_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
static const nir_search_expression search212_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0_1_0_0.value, &search212_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search212_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_0_0_0_1_0.value, &search212_0_0_0_1_1.value },
};
static const nir_search_expression search212_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0_0_0_0.value, &search212_0_0_0_1.value },
};

static const nir_search_constant search212_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x55555555 /* 1431655765 */ },
};
static const nir_search_expression search212_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_0_0_0.value, &search212_0_0_1.value },
};

static const nir_search_constant search212_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search212_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_0_0.value, &search212_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search212_1_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search212_1_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_0_0_0_1_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0_0_0_0_1_0_0.value, &search212_1_0_0_0_0_0_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_0_0_0_1_0.value, &search212_1_0_0_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search212_1_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_1_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0_1_0_0_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_1_0_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search212_1_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_1_0_0_1_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0_1_0_0_1_0_0.value, &search212_1_0_0_0_0_0_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_1_0_0_1_0.value, &search212_1_0_0_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_1_0_0_0.value, &search212_1_0_0_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0_1_0_0.value, &search212_1_0_0_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_0_0_0_1_0.value, &search212_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0_0_0_0.value, &search212_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
static const nir_search_expression search212_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_0_0_0.value, &search212_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search212_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_0_0.value, &search212_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search212_1_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_0_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search212_1_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_0_0_0_1_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0_0_0_0_1_0_0.value, &search212_1_0_0_1_0_0_0_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_0_0_0_1_0.value, &search212_1_0_0_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0_0_0_0.value, &search212_1_0_0_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_0_0.value, &search212_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search212_1_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_1_0_0_0_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0_1_0_0_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_1_0_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search212_1_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
};

static const nir_search_variable search212_1_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search212_1_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_1_0_0_1_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0_1_0_0_1_0_0.value, &search212_1_0_0_1_0_0_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_1_0_0_1_0.value, &search212_1_0_0_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_1_0_0_0.value, &search212_1_0_0_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0_1_0_0.value, &search212_1_0_0_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search212_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0_0_1_0.value, &search212_1_0_0_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_1_0_0_0.value, &search212_1_0_0_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
static const nir_search_expression search212_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0_1_0_0.value, &search212_1_0_0_1_0_1.value },
};

static const nir_search_constant search212_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search212_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0_0_1_0.value, &search212_1_0_0_1_1.value },
};
static const nir_search_expression search212_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_1_0_0_0.value, &search212_1_0_0_1.value },
};

static const nir_search_constant search212_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xaaaaaaaa /* 2863311530 */ },
};
static const nir_search_expression search212_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search212_1_0_0.value, &search212_1_0_1.value },
};

static const nir_search_constant search212_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search212_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search212_1_0.value, &search212_1_1.value },
};
static const nir_search_expression search212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search212_0.value, &search212_1.value },
};
   
static const nir_search_variable replace212_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_reverse,
   { &replace212_0.value },
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search77, &replace77.value, 0 },
   { &search78, &replace78.value, 0 },
   { &search79, &replace79.value, 0 },
   { &search80, &replace80.value, 0 },
   { &search107, &replace107.value, 0 },
   { &search108, &replace108.value, 0 },
   { &search114, &replace114.value, 0 },
   { &search212, &replace212.value, 0 },
};
   
static const nir_search_variable search174_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search174_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search174 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search174_0.value, &search174_1.value },
};
   
static const nir_search_variable replace174 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search175_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search175_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search175 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search175_0.value, &search175_1.value },
};
   
static const nir_search_constant replace175 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ussub_4x8_xforms[] = {
   { &search174, &replace174.value, 0 },
   { &search175, &replace175.value, 0 },
};
   
static const nir_search_variable search209_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_4x8,
   { &search209_0.value },
};
   
static const nir_search_constant replace209_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace209_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace209_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace209_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace209_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace209_1_1_0_0_0_0.value, &replace209_1_1_0_0_0_1.value },
};

static const nir_search_variable replace209_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace209_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace209_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace209_1_1_0_0_1_0.value, &replace209_1_1_0_0_1_1.value },
};

static const nir_search_variable replace209_1_1_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace209_1_1_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace209_1_1_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace209_1_1_0_0_2_0.value, &replace209_1_1_0_0_2_1.value },
};

static const nir_search_variable replace209_1_1_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace209_1_1_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace209_1_1_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace209_1_1_0_0_3_0.value, &replace209_1_1_0_0_3_1.value },
};
static const nir_search_expression replace209_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace209_1_1_0_0_0.value, &replace209_1_1_0_0_1.value, &replace209_1_1_0_0_2.value, &replace209_1_1_0_0_3.value },
};
static const nir_search_expression replace209_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f,
   { &replace209_1_1_0_0.value },
};

static const nir_search_constant replace209_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
static const nir_search_expression replace209_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace209_1_1_0.value, &replace209_1_1_1.value },
};
static const nir_search_expression replace209_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace209_1_0.value, &replace209_1_1.value },
};
static const nir_search_expression replace209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace209_0.value, &replace209_1.value },
};

static const struct transform nir_opt_algebraic_unpack_snorm_4x8_xforms[] = {
   { &search209, &replace209.value, 35 },
};
   
static const nir_search_variable search192_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search192_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frem,
   { &search192_0.value, &search192_1.value },
};
   
static const nir_search_variable replace192_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace192_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace192_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace192_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace192_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace192_1_1_0_0.value, &replace192_1_1_0_1.value },
};
static const nir_search_expression replace192_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &replace192_1_1_0.value },
};
static const nir_search_expression replace192_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace192_1_0.value, &replace192_1_1.value },
};
static const nir_search_expression replace192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace192_0.value, &replace192_1.value },
};

static const struct transform nir_opt_algebraic_frem_xforms[] = {
   { &search192, &replace192.value, 20 },
};
   
static const nir_search_variable search206_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_2x16,
   { &search206_0.value },
};
   
static const nir_search_variable replace206_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace206_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace206_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace206_0_0_0_0.value, &replace206_0_0_0_1.value },
};

static const nir_search_variable replace206_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace206_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace206_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace206_0_0_1_0.value, &replace206_0_0_1_1.value },
};
static const nir_search_expression replace206_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace206_0_0_0.value, &replace206_0_0_1.value },
};
static const nir_search_expression replace206_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace206_0_0.value },
};

static const nir_search_constant replace206_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
static const nir_search_expression replace206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace206_0.value, &replace206_1.value },
};

static const struct transform nir_opt_algebraic_unpack_unorm_2x16_xforms[] = {
   { &search206, &replace206.value, 32 },
};
   
static const nir_search_variable search208_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_2x16,
   { &search208_0.value },
};
   
static const nir_search_constant replace208_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace208_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace208_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace208_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace208_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace208_1_1_0_0_0_0.value, &replace208_1_1_0_0_0_1.value },
};

static const nir_search_variable replace208_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace208_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace208_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace208_1_1_0_0_1_0.value, &replace208_1_1_0_0_1_1.value },
};
static const nir_search_expression replace208_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace208_1_1_0_0_0.value, &replace208_1_1_0_0_1.value },
};
static const nir_search_expression replace208_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f,
   { &replace208_1_1_0_0.value },
};

static const nir_search_constant replace208_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
static const nir_search_expression replace208_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace208_1_1_0.value, &replace208_1_1_1.value },
};
static const nir_search_expression replace208_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace208_1_0.value, &replace208_1_1.value },
};
static const nir_search_expression replace208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace208_0.value, &replace208_1.value },
};

static const struct transform nir_opt_algebraic_unpack_snorm_2x16_xforms[] = {
   { &search208, &replace208.value, 34 },
};
   
static const nir_search_variable search66_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search66_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search66_0.value, &search66_1.value },
};
   
static const nir_search_variable replace66 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_imax_xforms[] = {
   { &search66, &replace66.value, 0 },
};
   
static const nir_search_variable search71_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search71_0.value },
};
   
static const nir_search_variable replace71_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace71_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace71_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace71_0_0.value, &replace71_0_1.value },
};

static const nir_search_constant replace71_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace71_0.value, &replace71_1.value },
};
   
static const nir_search_variable search72_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search72_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search72_0_0.value },
};
static const nir_search_expression search72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search72_0.value },
};
   
static const nir_search_variable replace72_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace72_0.value },
};
   
static const nir_search_variable search93_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search93_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search93_0_0_0.value },
};

static const nir_search_variable search93_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search93_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search93_0_1_0.value },
};
static const nir_search_expression search93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search93_0_0.value, &search93_0_1.value },
};
static const nir_search_expression search93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search93_0.value },
};
   
static const nir_search_variable replace93_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace93_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace93_0_0.value, &replace93_0_1.value },
};
static const nir_search_expression replace93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace93_0.value },
};

static const struct transform nir_opt_algebraic_fsat_xforms[] = {
   { &search71, &replace71.value, 9 },
   { &search72, &replace72.value, 0 },
   { &search93, &replace93.value, 0 },
};
   
static const nir_search_variable search201_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search201_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &search201_0.value, &search201_1.value },
};
   
static const nir_search_variable replace201_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace201_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace201_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace201_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace201_0_1_0.value, &replace201_0_1_1.value },
};
static const nir_search_expression replace201_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace201_0_0.value, &replace201_0_1.value },
};

static const nir_search_constant replace201_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};
static const nir_search_expression replace201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace201_0.value, &replace201_1.value },
};

static const struct transform nir_opt_algebraic_extract_u16_xforms[] = {
   { &search201, &replace201.value, 27 },
};
   
static const nir_search_constant search55_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search55_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search55_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search55_1_0.value },
};
static const nir_search_expression search55 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search55_0.value, &search55_1.value },
};
   
static const nir_search_variable replace55_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace55 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace55_0.value },
};
   
static const nir_search_variable search57_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search57_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search57_0_0_0.value },
};
static const nir_search_expression search57_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search57_0_0.value },
};

static const nir_search_constant search57_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search57 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search57_0.value, &search57_1.value },
};
   
static const nir_search_variable replace57_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace57_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace57 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace57_0.value, &replace57_1.value },
};
   
static const nir_search_variable search215_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search215_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search215_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search215_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search215_0_0.value, &search215_0_1.value, &search215_0_2.value },
};

static const nir_search_variable search215_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search215_0.value, &search215_1.value },
};
   
static const nir_search_variable replace215_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace215_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace215_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace215_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace215_1_0.value, &replace215_1_1.value },
};

static const nir_search_variable replace215_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace215_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace215_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace215_2_0.value, &replace215_2_1.value },
};
static const nir_search_expression replace215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace215_0.value, &replace215_1.value, &replace215_2.value },
};
   
static const nir_search_variable search216_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search216_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search216_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search216_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search216_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search216_1_0.value, &search216_1_1.value, &search216_1_2.value },
};
static const nir_search_expression search216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search216_0.value, &search216_1.value },
};
   
static const nir_search_variable replace216_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace216_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace216_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace216_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace216_1_0.value, &replace216_1_1.value },
};

static const nir_search_variable replace216_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace216_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace216_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace216_2_0.value, &replace216_2_1.value },
};
static const nir_search_expression replace216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace216_0.value, &replace216_1.value, &replace216_2.value },
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search55, &replace55.value, 0 },
   { &search57, &replace57.value, 0 },
   { &search215, &replace215.value, 0 },
   { &search216, &replace216.value, 0 },
};
   
static const nir_search_variable search135_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search135_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search135_0_0.value },
};
static const nir_search_expression search135 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search135_0.value },
};
   
static const nir_search_variable replace135_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace135_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace135_0_0.value },
};
static const nir_search_expression replace135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace135_0.value },
};
   
static const nir_search_variable search144_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search144_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search144_0_0.value },
};
static const nir_search_expression search144 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search144_0.value },
};
   
static const nir_search_variable replace144 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search145_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search145_0_0.value },
};
static const nir_search_expression search145 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search145_0.value },
};
   
static const nir_search_variable replace145_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace145 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace145_0.value },
};
   
static const nir_search_variable search147_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search147_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search147_0_0.value },
};
static const nir_search_expression search147 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search147_0.value },
};
   
static const nir_search_variable replace147_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace147 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &replace147_0.value },
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search135, &replace135.value, 0 },
   { &search144, &replace144.value, 0 },
   { &search145, &replace145.value, 0 },
   { &search147, &replace147.value, 15 },
};
   
static const nir_search_variable search109_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search109_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search109 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fxor,
   { &search109_0.value, &search109_1.value },
};
   
static const nir_search_constant replace109 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search109, &replace109.value, 0 },
};
   
static const nir_search_constant search119_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search119_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search119 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search119_0.value, &search119_1.value },
};
   
static const nir_search_constant replace119 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search120_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search120_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search120 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search120_0.value, &search120_1.value },
};
   
static const nir_search_variable replace120 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search166_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search166_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression search166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search166_0.value, &search166_1.value },
};
   
static const nir_search_variable replace166_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace166_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace166_0.value, &replace166_1.value },
};
   
static const nir_search_variable search170_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search170_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search170_0.value, &search170_1.value },
};
   
static const nir_search_variable replace170_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace170_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace170_0.value, &replace170_1.value },
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search119, &replace119.value, 0 },
   { &search120, &replace120.value, 0 },
   { &search166, &replace166.value, 16 },
   { &search170, &replace170.value, 17 },
};
   
static const nir_search_variable search123_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search123_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search123_0_0.value },
};
static const nir_search_expression search123 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search123_0.value },
};
   
static const nir_search_variable replace123 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search126_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search126_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search126_0_0_0.value },
};

static const nir_search_variable search126_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search126_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search126_0_0.value, &search126_0_1.value },
};
static const nir_search_expression search126 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search126_0.value },
};
   
static const nir_search_variable replace126_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace126_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace126 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace126_0.value, &replace126_1.value },
};
   
static const nir_search_variable search127_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search127_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search127_0_0_0_0.value },
};

static const nir_search_variable search127_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search127_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search127_0_0_0.value, &search127_0_0_1.value },
};

static const nir_search_variable search127_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search127_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search127_0_1_0_0.value },
};

static const nir_search_variable search127_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression search127_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search127_0_1_0.value, &search127_0_1_1.value },
};
static const nir_search_expression search127_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search127_0_0.value, &search127_0_1.value },
};
static const nir_search_expression search127 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search127_0.value },
};
   
static const nir_search_variable replace127_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace127_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace127_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace127_0_0.value, &replace127_0_1.value },
};

static const nir_search_variable replace127_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace127_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace127_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace127_1_0.value, &replace127_1_1.value },
};
static const nir_search_expression replace127 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &replace127_0.value, &replace127_1.value },
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search123, &replace123.value, 0 },
   { &search126, &replace126.value, 12 },
   { &search127, &replace127.value, 12 },
};
   
static const nir_search_constant search117_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search117_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search117_0.value, &search117_1.value },
};
   
static const nir_search_constant replace117 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search118_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search118_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search118 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search118_0.value, &search118_1.value },
};
   
static const nir_search_variable replace118 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search117, &replace117.value, 0 },
   { &search118, &replace118.value, 0 },
};
   
static const nir_search_variable search85_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search85_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search85_0.value, &search85_1.value },
};
   
static const nir_search_variable replace85_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace85_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace85_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace85_0_0.value, &replace85_0_1.value },
};
static const nir_search_expression replace85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace85_0.value },
};

static const struct transform nir_opt_algebraic_slt_xforms[] = {
   { &search85, &replace85.value, 10 },
};
   
static const nir_search_variable search160_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search160_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search160_0_0.value },
};
static const nir_search_expression search160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &search160_0.value },
};
   
static const nir_search_variable replace160_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i,
   { &replace160_0.value },
};

static const struct transform nir_opt_algebraic_f2i_xforms[] = {
   { &search160, &replace160.value, 0 },
};
   
static const nir_search_constant search56_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search56_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search56_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search56_1_0.value },
};
static const nir_search_expression search56 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search56_0.value, &search56_1.value },
};
   
static const nir_search_variable replace56_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace56_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace56 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace56_0.value, &replace56_1.value },
};
   
static const nir_search_variable search95_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search95_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search95_0_0_0.value },
};
static const nir_search_expression search95_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search95_0_0.value },
};

static const nir_search_constant search95_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search95_0.value, &search95_1.value },
};
   
static const nir_search_variable replace95 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search96_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search96_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search96_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search96_0_1_0.value },
};
static const nir_search_expression search96_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search96_0_0.value, &search96_0_1.value },
};

static const nir_search_constant search96_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search96_0.value, &search96_1.value },
};
   
static const nir_search_variable replace96 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search213_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search213_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search213_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search213_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search213_0_0.value, &search213_0_1.value, &search213_0_2.value },
};

static const nir_search_variable search213_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search213_0.value, &search213_1.value },
};
   
static const nir_search_variable replace213_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace213_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace213_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace213_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace213_1_0.value, &replace213_1_1.value },
};

static const nir_search_variable replace213_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace213_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace213_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace213_2_0.value, &replace213_2_1.value },
};
static const nir_search_expression replace213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace213_0.value, &replace213_1.value, &replace213_2.value },
};
   
static const nir_search_variable search214_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search214_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search214_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search214_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search214_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search214_1_0.value, &search214_1_1.value, &search214_1_2.value },
};
static const nir_search_expression search214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search214_0.value, &search214_1.value },
};
   
static const nir_search_variable replace214_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace214_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace214_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace214_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace214_1_0.value, &replace214_1_1.value },
};

static const nir_search_variable replace214_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace214_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace214_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace214_2_0.value, &replace214_2_1.value },
};
static const nir_search_expression replace214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace214_0.value, &replace214_1.value, &replace214_2.value },
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search56, &replace56.value, 0 },
   { &search95, &replace95.value, 0 },
   { &search96, &replace96.value, 0 },
   { &search213, &replace213.value, 0 },
   { &search214, &replace214.value, 0 },
};
   
static const nir_search_variable search101_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search101_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search101_0.value, &search101_1.value },
};
   
static const nir_search_constant replace101 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search229_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search229_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search229_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search229_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search229_0_0.value, &search229_0_1.value, &search229_0_2.value },
};

static const nir_search_variable search229_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search229_0.value, &search229_1.value },
};
   
static const nir_search_variable replace229_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace229_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace229_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace229_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace229_1_0.value, &replace229_1_1.value },
};

static const nir_search_variable replace229_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace229_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace229_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace229_2_0.value, &replace229_2_1.value },
};
static const nir_search_expression replace229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace229_0.value, &replace229_1.value, &replace229_2.value },
};
   
static const nir_search_variable search230_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search230_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search230_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search230_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search230_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search230_1_0.value, &search230_1_1.value, &search230_1_2.value },
};
static const nir_search_expression search230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search230_0.value, &search230_1.value },
};
   
static const nir_search_variable replace230_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace230_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace230_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace230_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace230_1_0.value, &replace230_1_1.value },
};

static const nir_search_variable replace230_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace230_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace230_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace230_2_0.value, &replace230_2_1.value },
};
static const nir_search_expression replace230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace230_0.value, &replace230_1.value, &replace230_2.value },
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search101, &replace101.value, 0 },
   { &search229, &replace229.value, 0 },
   { &search230, &replace230.value, 0 },
};
   
static const nir_search_variable search172_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search172_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search172_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search172_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search172_1_0.value, &search172_1_1.value },
};
static const nir_search_expression search172 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsub,
   { &search172_0.value, &search172_1.value },
};
   
static const nir_search_variable replace172_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace172_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace172 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace172_0.value, &replace172_1.value },
};
   
static const nir_search_variable search176_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search176_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search176 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search176_0.value, &search176_1.value },
};
   
static const nir_search_variable replace176_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace176_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace176_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace176_1_0.value },
};
static const nir_search_expression replace176 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace176_0.value, &replace176_1.value },
};

static const struct transform nir_opt_algebraic_fsub_xforms[] = {
   { &search172, &replace172.value, 0 },
   { &search176, &replace176.value, 18 },
};
   
static const nir_search_variable search0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search0_0_0.value },
};
static const nir_search_expression search0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search0_0.value },
};
   
static const nir_search_variable replace0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search178_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search178_0.value },
};
   
static const nir_search_constant replace178_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable replace178_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace178_0.value, &replace178_1.value },
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search0, &replace0.value, 0 },
   { &search178, &replace178.value, 19 },
};
   
static const nir_search_variable search89_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search89_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search89_0_0.value },
};

static const nir_search_variable search89_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search89_0.value, &search89_1.value },
};
   
static const nir_search_variable replace89_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace89_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace89_0.value, &replace89_1.value },
};
   
static const nir_search_variable search219_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search219_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search219_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search219_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search219_0_0.value, &search219_0_1.value, &search219_0_2.value },
};

static const nir_search_variable search219_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search219_0.value, &search219_1.value },
};
   
static const nir_search_variable replace219_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace219_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace219_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace219_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace219_1_0.value, &replace219_1_1.value },
};

static const nir_search_variable replace219_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace219_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace219_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace219_2_0.value, &replace219_2_1.value },
};
static const nir_search_expression replace219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace219_0.value, &replace219_1.value, &replace219_2.value },
};
   
static const nir_search_variable search220_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search220_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search220_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search220_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search220_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search220_1_0.value, &search220_1_1.value, &search220_1_2.value },
};
static const nir_search_expression search220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search220_0.value, &search220_1.value },
};
   
static const nir_search_variable replace220_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace220_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace220_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace220_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace220_1_0.value, &replace220_1_1.value },
};

static const nir_search_variable replace220_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace220_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace220_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace220_2_0.value, &replace220_2_1.value },
};
static const nir_search_expression replace220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace220_0.value, &replace220_1.value, &replace220_2.value },
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search89, &replace89.value, 0 },
   { &search219, &replace219.value, 0 },
   { &search220, &replace220.value, 0 },
};
   
static const nir_search_variable search4_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search4_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search4_0_0.value },
};
static const nir_search_expression search4 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search4_0.value },
};
   
static const nir_search_variable replace4_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace4 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace4_0.value },
};
   
static const nir_search_variable search5_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search5_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search5_0_0.value },
};
static const nir_search_expression search5 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search5_0.value },
};
   
static const nir_search_variable replace5_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace5 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace5_0.value },
};
   
static const nir_search_variable search165_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search165_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search165_0_0.value },
};
static const nir_search_expression search165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search165_0.value },
};
   
static const nir_search_variable replace165_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace165_0.value },
};
   
static const nir_search_constant search183_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search183_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search183_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search183_0_0.value, &search183_0_1.value },
};
static const nir_search_expression search183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search183_0.value },
};
   
static const nir_search_variable replace183_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace183_0.value },
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search4, &replace4.value, 0 },
   { &search5, &replace5.value, 0 },
   { &search165, &replace165.value, 0 },
   { &search183, &replace183.value, 0 },
};
   
static const nir_search_variable search207_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};
static const nir_search_expression search207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_4x8,
   { &search207_0.value },
};
   
static const nir_search_variable replace207_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace207_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace207_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace207_0_0_0_0.value, &replace207_0_0_0_1.value },
};

static const nir_search_variable replace207_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace207_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace207_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace207_0_0_1_0.value, &replace207_0_0_1_1.value },
};

static const nir_search_variable replace207_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace207_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace207_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace207_0_0_2_0.value, &replace207_0_0_2_1.value },
};

static const nir_search_variable replace207_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace207_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace207_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace207_0_0_3_0.value, &replace207_0_0_3_1.value },
};
static const nir_search_expression replace207_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace207_0_0_0.value, &replace207_0_0_1.value, &replace207_0_0_2.value, &replace207_0_0_3.value },
};
static const nir_search_expression replace207_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f,
   { &replace207_0_0.value },
};

static const nir_search_constant replace207_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
static const nir_search_expression replace207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace207_0.value, &replace207_1.value },
};

static const struct transform nir_opt_algebraic_unpack_unorm_4x8_xforms[] = {
   { &search207, &replace207.value, 33 },
};
   
static const nir_search_variable search63_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search63_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search63 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search63_0.value, &search63_1.value },
};
   
static const nir_search_variable replace63 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search69_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search69_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search69_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search69_0_0.value, &search69_0_1.value },
};

static const nir_search_constant search69_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search69 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmin,
   { &search69_0.value, &search69_1.value },
};
   
static const nir_search_variable replace69_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace69_0.value },
};
   
static const nir_search_variable search73_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search73_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search73_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search73_0_0_0_0.value, &search73_0_0_0_1.value },
};

static const nir_search_variable search73_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search73_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search73_0_0_0.value, &search73_0_0_1.value },
};

static const nir_search_variable search73_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search73_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search73_0_0.value, &search73_0_1.value },
};

static const nir_search_variable search73_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search73_0.value, &search73_1.value },
};
   
static const nir_search_variable replace73_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace73_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace73_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace73_0_0.value, &replace73_0_1.value },
};

static const nir_search_variable replace73_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace73_0.value, &replace73_1.value },
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search63, &replace63.value, 0 },
   { &search69, &replace69.value, 8 },
   { &search73, &replace73.value, 0 },
};
   
static const nir_search_variable search100_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search100_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search100_0.value, &search100_1.value },
};
   
static const nir_search_constant replace100 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search149_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
};

static const nir_search_constant search149_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search149 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search149_0.value, &search149_1.value },
};
   
static const nir_search_variable replace149_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace149 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace149_0.value },
};
   
static const nir_search_variable search150_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
};

static const nir_search_constant search150_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search150 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search150_0.value, &search150_1.value },
};
   
static const nir_search_variable replace150 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search227_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search227_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search227_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search227_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search227_0_0.value, &search227_0_1.value, &search227_0_2.value },
};

static const nir_search_variable search227_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search227_0.value, &search227_1.value },
};
   
static const nir_search_variable replace227_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace227_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace227_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace227_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace227_1_0.value, &replace227_1_1.value },
};

static const nir_search_variable replace227_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace227_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace227_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace227_2_0.value, &replace227_2_1.value },
};
static const nir_search_expression replace227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace227_0.value, &replace227_1.value, &replace227_2.value },
};
   
static const nir_search_variable search228_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search228_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search228_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search228_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search228_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search228_1_0.value, &search228_1_1.value, &search228_1_2.value },
};
static const nir_search_expression search228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search228_0.value, &search228_1.value },
};
   
static const nir_search_variable replace228_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace228_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace228_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace228_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace228_1_0.value, &replace228_1_1.value },
};

static const nir_search_variable replace228_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace228_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace228_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace228_2_0.value, &replace228_2_1.value },
};
static const nir_search_expression replace228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace228_0.value, &replace228_1.value, &replace228_2.value },
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search100, &replace100.value, 0 },
   { &search149, &replace149.value, 0 },
   { &search150, &replace150.value, 0 },
   { &search227, &replace227.value, 0 },
   { &search228, &replace228.value, 0 },
};
   
static const nir_search_variable search159_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search159_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search159_0_0.value },
};
static const nir_search_expression search159 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search159_0.value },
};
   
static const nir_search_variable replace159 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search162_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search162_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search162_0_0.value },
};
static const nir_search_expression search162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search162_0.value },
};
   
static const nir_search_variable replace162_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace162_0.value },
};
   
static const nir_search_variable search163_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search163_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search163_0_0.value },
};
static const nir_search_expression search163 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search163_0.value },
};
   
static const nir_search_variable replace163_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace163 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace163_0.value },
};

static const struct transform nir_opt_algebraic_i2b_xforms[] = {
   { &search159, &replace159.value, 0 },
   { &search162, &replace162.value, 0 },
   { &search163, &replace163.value, 0 },
};
   
static const nir_search_variable search198_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search198_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &search198_0.value, &search198_1.value },
};
   
static const nir_search_variable replace198_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace198_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};

static const nir_search_variable replace198_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace198_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace198_0_1_0_0.value, &replace198_0_1_0_1.value },
};

static const nir_search_constant replace198_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression replace198_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace198_0_1_0.value, &replace198_0_1_1.value },
};
static const nir_search_expression replace198_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace198_0_0.value, &replace198_0_1.value },
};

static const nir_search_constant replace198_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression replace198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace198_0.value, &replace198_1.value },
};

static const struct transform nir_opt_algebraic_extract_i8_xforms[] = {
   { &search198, &replace198.value, 26 },
};
   
static const nir_search_variable search193_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search193_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search193 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_uadd_carry,
   { &search193_0.value, &search193_1.value },
};
   
static const nir_search_variable replace193_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace193_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace193_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace193_0_0_0.value, &replace193_0_0_1.value },
};

static const nir_search_variable replace193_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace193_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace193_0_0.value, &replace193_0_1.value },
};
static const nir_search_expression replace193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace193_0.value },
};

static const struct transform nir_opt_algebraic_uadd_carry_xforms[] = {
   { &search193, &replace193.value, 22 },
};
   
static const nir_search_variable search196_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
};

static const nir_search_variable search196_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
};

static const nir_search_variable search196_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression search196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibitfield_extract,
   { &search196_0.value, &search196_1.value, &search196_2.value },
};
   
static const nir_search_constant replace196_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace196_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace196_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace196_0_0.value, &replace196_0_1.value },
};

static const nir_search_variable replace196_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace196_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace196_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace196_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace196_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibfe,
   { &replace196_2_0.value, &replace196_2_1.value, &replace196_2_2.value },
};
static const nir_search_expression replace196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace196_0.value, &replace196_1.value, &replace196_2.value },
};

static const struct transform nir_opt_algebraic_ibitfield_extract_xforms[] = {
   { &search196, &replace196.value, 25 },
};
   
static const nir_search_variable search210_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_variable search210_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};
static const nir_search_expression search210 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ldexp,
   { &search210_0.value, &search210_1.value },
};
   
static const nir_search_variable replace210_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace210_0_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace210_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace210_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace210_0_1_0_0_0_0_0.value, &replace210_0_1_0_0_0_0_1.value },
};

static const nir_search_constant replace210_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace210_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace210_0_1_0_0_0_0.value, &replace210_0_1_0_0_0_1.value },
};

static const nir_search_constant replace210_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace210_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace210_0_1_0_0_0.value, &replace210_0_1_0_0_1.value },
};

static const nir_search_constant replace210_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
static const nir_search_expression replace210_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace210_0_1_0_0.value, &replace210_0_1_0_1.value },
};

static const nir_search_constant replace210_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
static const nir_search_expression replace210_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace210_0_1_0.value, &replace210_0_1_1.value },
};
static const nir_search_expression replace210_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace210_0_0.value, &replace210_0_1.value },
};

static const nir_search_variable replace210_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace210_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace210_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace210_1_0_0_0_0_0.value, &replace210_1_0_0_0_0_1.value },
};

static const nir_search_constant replace210_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace210_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace210_1_0_0_0_0.value, &replace210_1_0_0_0_1.value },
};

static const nir_search_variable replace210_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace210_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace210_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace210_1_0_0_1_0_0_0.value, &replace210_1_0_0_1_0_0_1.value },
};

static const nir_search_constant replace210_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace210_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace210_1_0_0_1_0_0.value, &replace210_1_0_0_1_0_1.value },
};

static const nir_search_constant replace210_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace210_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace210_1_0_0_1_0.value, &replace210_1_0_0_1_1.value },
};
static const nir_search_expression replace210_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace210_1_0_0_0.value, &replace210_1_0_0_1.value },
};

static const nir_search_constant replace210_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
static const nir_search_expression replace210_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace210_1_0_0.value, &replace210_1_0_1.value },
};

static const nir_search_constant replace210_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
static const nir_search_expression replace210_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace210_1_0.value, &replace210_1_1.value },
};
static const nir_search_expression replace210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace210_0.value, &replace210_1.value },
};
   
static const nir_search_variable search211_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_variable search211_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};
static const nir_search_expression search211 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_ldexp,
   { &search211_0.value, &search211_1.value },
};
   
static const nir_search_variable replace211_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace211_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace211_0_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace211_0_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace211_0_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace211_0_1_1_0_0_0_0_0.value, &replace211_0_1_1_0_0_0_0_1.value },
};

static const nir_search_constant replace211_0_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace211_0_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace211_0_1_1_0_0_0_0.value, &replace211_0_1_1_0_0_0_1.value },
};

static const nir_search_constant replace211_0_1_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace211_0_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace211_0_1_1_0_0_0.value, &replace211_0_1_1_0_0_1.value },
};

static const nir_search_constant replace211_0_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
static const nir_search_expression replace211_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace211_0_1_1_0_0.value, &replace211_0_1_1_0_1.value },
};

static const nir_search_constant replace211_0_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
static const nir_search_expression replace211_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace211_0_1_1_0.value, &replace211_0_1_1_1.value },
};
static const nir_search_expression replace211_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_double_2x32_split,
   { &replace211_0_1_0.value, &replace211_0_1_1.value },
};
static const nir_search_expression replace211_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace211_0_0.value, &replace211_0_1.value },
};

static const nir_search_constant replace211_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace211_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace211_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace211_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace211_1_1_0_0_0_0_0.value, &replace211_1_1_0_0_0_0_1.value },
};

static const nir_search_constant replace211_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace211_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace211_1_1_0_0_0_0.value, &replace211_1_1_0_0_0_1.value },
};

static const nir_search_variable replace211_1_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace211_1_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace211_1_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace211_1_1_0_0_1_0_0_0.value, &replace211_1_1_0_0_1_0_0_1.value },
};

static const nir_search_constant replace211_1_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace211_1_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace211_1_1_0_0_1_0_0.value, &replace211_1_1_0_0_1_0_1.value },
};

static const nir_search_constant replace211_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace211_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace211_1_1_0_0_1_0.value, &replace211_1_1_0_0_1_1.value },
};
static const nir_search_expression replace211_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace211_1_1_0_0_0.value, &replace211_1_1_0_0_1.value },
};

static const nir_search_constant replace211_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
static const nir_search_expression replace211_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace211_1_1_0_0.value, &replace211_1_1_0_1.value },
};

static const nir_search_constant replace211_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
static const nir_search_expression replace211_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace211_1_1_0.value, &replace211_1_1_1.value },
};
static const nir_search_expression replace211_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_double_2x32_split,
   { &replace211_1_0.value, &replace211_1_1.value },
};
static const nir_search_expression replace211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace211_0.value, &replace211_1.value },
};

static const struct transform nir_opt_algebraic_ldexp_xforms[] = {
   { &search210, &replace210.value, 0 },
   { &search211, &replace211.value, 0 },
};
   
static const nir_search_variable search200_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search200_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &search200_0.value, &search200_1.value },
};
   
static const nir_search_variable replace200_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace200_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};

static const nir_search_variable replace200_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace200_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace200_0_1_0_0.value, &replace200_0_1_0_1.value },
};

static const nir_search_constant replace200_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace200_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace200_0_1_0.value, &replace200_0_1_1.value },
};
static const nir_search_expression replace200_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace200_0_0.value, &replace200_0_1.value },
};

static const nir_search_constant replace200_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace200_0.value, &replace200_1.value },
};

static const struct transform nir_opt_algebraic_extract_i16_xforms[] = {
   { &search200, &replace200.value, 27 },
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
   bool condition_flags[36];
   const nir_shader_compiler_options *options = shader->options;
   (void) options;

   condition_flags[0] = true;
   condition_flags[1] = options->lower_flrp32;
   condition_flags[2] = options->lower_flrp64;
   condition_flags[3] = options->lower_ffract;
   condition_flags[4] = !options->lower_flrp32;
   condition_flags[5] = !options->lower_flrp64;
   condition_flags[6] = options->lower_ffma;
   condition_flags[7] = options->fuse_ffma;
   condition_flags[8] = !options->lower_fsat;
   condition_flags[9] = options->lower_fsat;
   condition_flags[10] = options->lower_scmp;
   condition_flags[11] = options->lower_fpow;
   condition_flags[12] = !options->lower_fpow;
   condition_flags[13] = options->lower_fdiv;
   condition_flags[14] = options->lower_fsqrt;
   condition_flags[15] = !options->lower_fsqrt;
   condition_flags[16] = !options->lower_extract_byte;
   condition_flags[17] = !options->lower_extract_word;
   condition_flags[18] = options->lower_sub;
   condition_flags[19] = options->lower_negate;
   condition_flags[20] = options->lower_fmod32;
   condition_flags[21] = options->lower_fmod64;
   condition_flags[22] = options->lower_uadd_carry;
   condition_flags[23] = options->lower_usub_borrow;
   condition_flags[24] = options->lower_bitfield_insert;
   condition_flags[25] = options->lower_bitfield_extract;
   condition_flags[26] = options->lower_extract_byte;
   condition_flags[27] = options->lower_extract_word;
   condition_flags[28] = options->lower_pack_unorm_2x16;
   condition_flags[29] = options->lower_pack_unorm_4x8;
   condition_flags[30] = options->lower_pack_snorm_2x16;
   condition_flags[31] = options->lower_pack_snorm_4x8;
   condition_flags[32] = options->lower_unpack_unorm_2x16;
   condition_flags[33] = options->lower_unpack_unorm_4x8;
   condition_flags[34] = options->lower_unpack_snorm_2x16;
   condition_flags[35] = options->lower_unpack_snorm_4x8;

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

   
static const nir_search_variable search239_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search239_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search239_0.value, &search239_1.value },
};
   
static const nir_search_variable replace239_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace239_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated4,
   { &replace239_0.value, &replace239_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot4_xforms[] = {
   { &search239, &replace239.value, 36 },
};
   
static const nir_search_variable search233_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search233_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search233_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search233_0_0.value, &search233_0_1.value },
};

static const nir_search_constant search233_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search233_0.value, &search233_1.value },
};
   
static const nir_search_variable replace233_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace233_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace233_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace233_1_0.value },
};
static const nir_search_expression replace233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace233_0.value, &replace233_1.value },
};

static const struct transform nir_opt_algebraic_late_flt_xforms[] = {
   { &search233, &replace233.value, 0 },
};
   
static const nir_search_variable search237_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search237_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot2,
   { &search237_0.value, &search237_1.value },
};
   
static const nir_search_variable replace237_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace237_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated2,
   { &replace237_0.value, &replace237_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot2_xforms[] = {
   { &search237, &replace237.value, 36 },
};
   
static const nir_search_variable search238_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search238_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search238_0.value, &search238_1.value },
};
   
static const nir_search_variable replace238_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace238_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated3,
   { &replace238_0.value, &replace238_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot3_xforms[] = {
   { &search238, &replace238.value, 36 },
};
   
static const nir_search_variable search236_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search236_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search236_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search236_0_0.value, &search236_0_1.value },
};

static const nir_search_constant search236_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search236 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fne,
   { &search236_0.value, &search236_1.value },
};
   
static const nir_search_variable replace236_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace236_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace236_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace236_1_0.value },
};
static const nir_search_expression replace236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace236_0.value, &replace236_1.value },
};

static const struct transform nir_opt_algebraic_late_fne_xforms[] = {
   { &search236, &replace236.value, 0 },
};
   
static const nir_search_variable search235_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search235_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search235_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search235_0_0.value, &search235_0_1.value },
};

static const nir_search_constant search235_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search235 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_feq,
   { &search235_0.value, &search235_1.value },
};
   
static const nir_search_variable replace235_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace235_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace235_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace235_1_0.value },
};
static const nir_search_expression replace235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace235_0.value, &replace235_1.value },
};

static const struct transform nir_opt_algebraic_late_feq_xforms[] = {
   { &search235, &replace235.value, 0 },
};
   
static const nir_search_variable search234_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search234_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search234_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search234_0_0.value, &search234_0_1.value },
};

static const nir_search_constant search234_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search234 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search234_0.value, &search234_1.value },
};
   
static const nir_search_variable replace234_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace234_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace234_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace234_1_0.value },
};
static const nir_search_expression replace234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace234_0.value, &replace234_1.value },
};

static const struct transform nir_opt_algebraic_late_fge_xforms[] = {
   { &search234, &replace234.value, 0 },
};
   
static const nir_search_variable search240_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search240_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph,
   { &search240_0.value, &search240_1.value },
};
   
static const nir_search_variable replace240_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace240_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph_replicated,
   { &replace240_0.value, &replace240_1.value },
};

static const struct transform nir_opt_algebraic_late_fdph_xforms[] = {
   { &search240, &replace240.value, 36 },
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
   bool condition_flags[37];
   const nir_shader_compiler_options *options = shader->options;
   (void) options;

   condition_flags[0] = true;
   condition_flags[1] = options->lower_flrp32;
   condition_flags[2] = options->lower_flrp64;
   condition_flags[3] = options->lower_ffract;
   condition_flags[4] = !options->lower_flrp32;
   condition_flags[5] = !options->lower_flrp64;
   condition_flags[6] = options->lower_ffma;
   condition_flags[7] = options->fuse_ffma;
   condition_flags[8] = !options->lower_fsat;
   condition_flags[9] = options->lower_fsat;
   condition_flags[10] = options->lower_scmp;
   condition_flags[11] = options->lower_fpow;
   condition_flags[12] = !options->lower_fpow;
   condition_flags[13] = options->lower_fdiv;
   condition_flags[14] = options->lower_fsqrt;
   condition_flags[15] = !options->lower_fsqrt;
   condition_flags[16] = !options->lower_extract_byte;
   condition_flags[17] = !options->lower_extract_word;
   condition_flags[18] = options->lower_sub;
   condition_flags[19] = options->lower_negate;
   condition_flags[20] = options->lower_fmod32;
   condition_flags[21] = options->lower_fmod64;
   condition_flags[22] = options->lower_uadd_carry;
   condition_flags[23] = options->lower_usub_borrow;
   condition_flags[24] = options->lower_bitfield_insert;
   condition_flags[25] = options->lower_bitfield_extract;
   condition_flags[26] = options->lower_extract_byte;
   condition_flags[27] = options->lower_extract_word;
   condition_flags[28] = options->lower_pack_unorm_2x16;
   condition_flags[29] = options->lower_pack_unorm_4x8;
   condition_flags[30] = options->lower_pack_snorm_2x16;
   condition_flags[31] = options->lower_pack_snorm_4x8;
   condition_flags[32] = options->lower_unpack_unorm_2x16;
   condition_flags[33] = options->lower_unpack_unorm_4x8;
   condition_flags[34] = options->lower_unpack_snorm_2x16;
   condition_flags[35] = options->lower_unpack_snorm_4x8;
   condition_flags[36] = options->fdot_replicates;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_algebraic_late_impl(function->impl, condition_flags);
   }

   return progress;
}

