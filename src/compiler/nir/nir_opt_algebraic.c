
#include "nir.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

   
static const nir_search_variable search0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search0_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
static const nir_search_expression search0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search0_0.value, &search0_1.value },
   NULL,
};
   
static const nir_search_variable replace0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace0_1_0.value },
   NULL,
};
static const nir_search_expression replace0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace0_0.value, &replace0_1.value },
   NULL,
};
   
static const nir_search_variable search1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search1_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_neg_power_of_two),
};
static const nir_search_expression search1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search1_0.value, &search1_1.value },
   NULL,
};
   
static const nir_search_variable replace1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace1_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace1_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace1_0_1_0_0.value },
   NULL,
};
static const nir_search_expression replace1_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace1_0_1_0.value },
   NULL,
};
static const nir_search_expression replace1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace1_0_0.value, &replace1_0_1.value },
   NULL,
};
static const nir_search_expression replace1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace1_0.value },
   NULL,
};
   
static const nir_search_variable search30_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search30_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search30 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search30_0.value, &search30_1.value },
   NULL,
};
   
static const nir_search_constant replace30 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search34_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search34_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search34 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search34_0.value, &search34_1.value },
   NULL,
};
   
static const nir_search_variable replace34 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search36_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search36_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search36_0.value, &search36_1.value },
   NULL,
};
   
static const nir_search_variable replace36_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace36 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace36_0.value },
   NULL,
};
   
static const nir_search_variable search221_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search221_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search221_0_0.value },
   NULL,
};

static const nir_search_variable search221_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search221_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search221_1_0.value },
   NULL,
};
static const nir_search_expression search221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search221_0.value, &search221_1.value },
   NULL,
};
   
static const nir_search_variable replace221_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace221_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace221_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace221_0_0.value, &replace221_0_1.value },
   NULL,
};
static const nir_search_expression replace221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace221_0.value },
   NULL,
};
   
static const nir_search_variable search333_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search333_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search333_0_0.value },
   NULL,
};

static const nir_search_variable search333_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search333 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search333_0.value, &search333_1.value },
   NULL,
};
   
static const nir_search_variable replace333_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace333_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace333_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace333_0_0.value, &replace333_0_1.value },
   NULL,
};
static const nir_search_expression replace333 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace333_0.value },
   NULL,
};
   
static const nir_search_variable search335_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search335_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search335_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search335_0_0.value, &search335_0_1.value },
   (is_used_once),
};

static const nir_search_variable search335_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search335 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search335_0.value, &search335_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace335_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace335_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace335_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace335_0_0.value, &replace335_0_1.value },
   NULL,
};

static const nir_search_variable replace335_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace335 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace335_0.value, &replace335_1.value },
   NULL,
};
   
static const nir_search_variable search339_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search339_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search339_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search339_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search339_1_0.value, &search339_1_1.value },
   NULL,
};
static const nir_search_expression search339 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search339_0.value, &search339_1.value },
   NULL,
};
   
static const nir_search_variable replace339_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace339_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace339_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace339_0_0.value, &replace339_0_1.value },
   NULL,
};

static const nir_search_variable replace339_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace339 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace339_0.value, &replace339_1.value },
   NULL,
};
   
static const nir_search_variable search428_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search428_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search428_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search428_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search428_0_0.value, &search428_0_1.value, &search428_0_2.value },
   (is_used_once),
};

static const nir_search_variable search428_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search428 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search428_0.value, &search428_1.value },
   NULL,
};
   
static const nir_search_variable replace428_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace428_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace428_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace428_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace428_1_0.value, &replace428_1_1.value },
   NULL,
};

static const nir_search_variable replace428_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace428_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace428_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace428_2_0.value, &replace428_2_1.value },
   NULL,
};
static const nir_search_expression replace428 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace428_0.value, &replace428_1.value, &replace428_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search0, &replace0.value, 0 },
   { &search1, &replace1.value, 0 },
   { &search30, &replace30.value, 0 },
   { &search34, &replace34.value, 0 },
   { &search36, &replace36.value, 0 },
   { &search221, &replace221.value, 0 },
   { &search333, &replace333.value, 0 },
   { &search335, &replace335.value, 0 },
   { &search339, &replace339.value, 0 },
   { &search428, &replace428.value, 0 },
};
   
static const nir_search_variable search2_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_udiv,
   { &search2_0.value, &search2_1.value },
   NULL,
};
   
static const nir_search_variable replace2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search6_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search6_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
static const nir_search_expression search6 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_udiv,
   { &search6_0.value, &search6_1.value },
   NULL,
};
   
static const nir_search_variable replace6_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace6_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace6_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace6_1_0.value },
   NULL,
};
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
   
static const nir_search_variable search3_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search3_0.value, &search3_1.value },
   NULL,
};
   
static const nir_search_variable replace3 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search7_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search7_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
static const nir_search_expression search7 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search7_0.value, &search7_1.value },
   NULL,
};
   
static const nir_search_variable replace7_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace7_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isign,
   { &replace7_0_0.value },
   NULL,
};

static const nir_search_variable replace7_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace7_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace7_1_0_0.value },
   NULL,
};

static const nir_search_variable replace7_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace7_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace7_1_1_0.value },
   NULL,
};
static const nir_search_expression replace7_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace7_1_0.value, &replace7_1_1.value },
   NULL,
};
static const nir_search_expression replace7 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace7_0.value, &replace7_1.value },
   NULL,
};
   
static const nir_search_variable search8_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search8_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_neg_power_of_two),
};
static const nir_search_expression search8 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_idiv,
   { &search8_0.value, &search8_1.value },
   NULL,
};
   
static const nir_search_variable replace8_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace8_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isign,
   { &replace8_0_0_0.value },
   NULL,
};

static const nir_search_variable replace8_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace8_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace8_0_1_0_0.value },
   NULL,
};

static const nir_search_variable replace8_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace8_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace8_0_1_1_0_0.value },
   NULL,
};
static const nir_search_expression replace8_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace8_0_1_1_0.value },
   NULL,
};
static const nir_search_expression replace8_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace8_0_1_0.value, &replace8_0_1_1.value },
   NULL,
};
static const nir_search_expression replace8_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace8_0_0.value, &replace8_0_1.value },
   NULL,
};
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
   
static const nir_search_variable search4_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search4_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search4 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umod,
   { &search4_0.value, &search4_1.value },
   NULL,
};
   
static const nir_search_constant replace4 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search9_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search9_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_pos_power_of_two),
};
static const nir_search_expression search9 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umod,
   { &search9_0.value, &search9_1.value },
   NULL,
};
   
static const nir_search_variable replace9_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace9_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace9_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace9_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace9_1_0.value, &replace9_1_1.value },
   NULL,
};
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
   
static const nir_search_variable search5_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search5_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search5 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imod,
   { &search5_0.value, &search5_1.value },
   NULL,
};
   
static const nir_search_constant replace5 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_imod_xforms[] = {
   { &search5, &replace5.value, 0 },
};
   
static const nir_search_variable search10_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search10_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search10_0_0.value },
   NULL,
};
static const nir_search_expression search10 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search10_0.value },
   NULL,
};
   
static const nir_search_variable replace10 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search326_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search326 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search326_0.value },
   NULL,
};
   
static const nir_search_constant replace326_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable replace326_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace326 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace326_0.value, &replace326_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search10, &replace10.value, 0 },
   { &search326, &replace326.value, 21 },
};
   
static const nir_search_variable search11_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search11_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search11_0_0.value },
   NULL,
};
static const nir_search_expression search11 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search11_0.value },
   NULL,
};
   
static const nir_search_variable replace11 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search225_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search225_0 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_b2i,
   { &search225_0_0.value },
   NULL,
};
static const nir_search_expression search225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search225_0.value },
   NULL,
};
   
static const nir_search_variable replace225 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search327_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search327 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search327_0.value },
   NULL,
};
   
static const nir_search_constant replace327_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace327_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace327 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace327_0.value, &replace327_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search11, &replace11.value, 0 },
   { &search225, &replace225.value, 0 },
   { &search327, &replace327.value, 21 },
};
   
static const nir_search_variable search12_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search12_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search12_0_0.value },
   NULL,
};
static const nir_search_expression search12 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search12_0.value },
   NULL,
};
   
static const nir_search_variable replace12_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace12 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace12_0.value },
   NULL,
};
   
static const nir_search_variable search13_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search13_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search13_0_0.value },
   NULL,
};
static const nir_search_expression search13 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search13_0.value },
   NULL,
};
   
static const nir_search_variable replace13_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace13 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace13_0.value },
   NULL,
};
   
static const nir_search_variable search14_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search14_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f32,
   { &search14_0_0.value },
   NULL,
};
static const nir_search_expression search14 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search14_0.value },
   NULL,
};
   
static const nir_search_variable replace14_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace14 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f32,
   { &replace14_0.value },
   NULL,
};
   
static const nir_search_variable search211_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search211_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search211_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search211_0_0.value, &search211_0_1.value },
   NULL,
};
static const nir_search_expression search211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search211_0.value },
   NULL,
};
   
static const nir_search_variable replace211_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace211_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &replace211_0.value, &replace211_1.value },
   NULL,
};
   
static const nir_search_variable search212_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search212_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search212_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search212_0_0.value, &search212_0_1.value },
   NULL,
};
static const nir_search_expression search212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search212_0.value },
   NULL,
};
   
static const nir_search_variable replace212_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace212_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &replace212_0.value, &replace212_1.value },
   NULL,
};
   
static const nir_search_variable search213_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search213_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search213_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search213_0_0.value, &search213_0_1.value },
   NULL,
};
static const nir_search_expression search213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search213_0.value },
   NULL,
};
   
static const nir_search_variable replace213_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace213_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &replace213_0.value, &replace213_1.value },
   NULL,
};
   
static const nir_search_variable search214_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search214_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search214_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search214_0_0.value, &search214_0_1.value },
   NULL,
};
static const nir_search_expression search214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search214_0.value },
   NULL,
};
   
static const nir_search_variable replace214_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace214_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &replace214_0.value, &replace214_1.value },
   NULL,
};
   
static const nir_search_variable search308_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search308_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search308_0_0.value },
   NULL,
};
static const nir_search_expression search308 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search308_0.value },
   NULL,
};
   
static const nir_search_variable replace308_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace308 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace308_0.value },
   NULL,
};
   
static const nir_search_constant search330_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search330_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search330_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search330_0_0.value, &search330_0_1.value },
   NULL,
};
static const nir_search_expression search330 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search330_0.value },
   NULL,
};
   
static const nir_search_variable replace330_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace330 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace330_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search12, &replace12.value, 0 },
   { &search13, &replace13.value, 0 },
   { &search14, &replace14.value, 0 },
   { &search211, &replace211.value, 0 },
   { &search212, &replace212.value, 0 },
   { &search213, &replace213.value, 0 },
   { &search214, &replace214.value, 0 },
   { &search308, &replace308.value, 0 },
   { &search330, &replace330.value, 0 },
};
   
static const nir_search_variable search15_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search15_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search15_0_0.value },
   NULL,
};
static const nir_search_expression search15 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search15_0.value },
   NULL,
};
   
static const nir_search_variable replace15_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace15 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace15_0.value },
   NULL,
};
   
static const nir_search_variable search16_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search16_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search16_0_0.value },
   NULL,
};
static const nir_search_expression search16 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search16_0.value },
   NULL,
};
   
static const nir_search_variable replace16_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace16 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace16_0.value },
   NULL,
};
   
static const nir_search_variable search309_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search309_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search309_0_0.value },
   NULL,
};
static const nir_search_expression search309 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search309_0.value },
   NULL,
};
   
static const nir_search_variable replace309_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace309 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace309_0.value },
   NULL,
};
   
static const nir_search_constant search331_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search331_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search331_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search331_0_0.value, &search331_0_1.value },
   NULL,
};
static const nir_search_expression search331 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search331_0.value },
   NULL,
};
   
static const nir_search_variable replace331_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace331 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace331_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search15, &replace15.value, 0 },
   { &search16, &replace16.value, 0 },
   { &search309, &replace309.value, 0 },
   { &search331, &replace331.value, 0 },
};
   
static const nir_search_variable search17_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search17_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search17 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search17_0.value, &search17_1.value },
   NULL,
};
   
static const nir_search_variable replace17 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search21_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search21_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search21_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search21_0_0.value, &search21_0_1.value },
   NULL,
};

static const nir_search_variable search21_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search21_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search21_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search21_1_0.value, &search21_1_1.value },
   NULL,
};
static const nir_search_expression search21 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search21_0.value, &search21_1.value },
   NULL,
};
   
static const nir_search_variable replace21_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace21_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace21_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace21_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace21_1_0.value, &replace21_1_1.value },
   NULL,
};
static const nir_search_expression replace21 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace21_0.value, &replace21_1.value },
   NULL,
};
   
static const nir_search_variable search23_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search23_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search23_0_0.value },
   NULL,
};

static const nir_search_variable search23_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search23 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search23_0.value, &search23_1.value },
   NULL,
};
   
static const nir_search_constant replace23 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search27_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search27_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search27_0_0.value },
   NULL,
};

static const nir_search_variable search27_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search27_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search27_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search27_1_0.value, &search27_1_1.value },
   NULL,
};
static const nir_search_expression search27 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search27_0.value, &search27_1.value },
   NULL,
};
   
static const nir_search_variable replace27 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search28_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search28_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search28_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search28_1_0_0.value },
   NULL,
};

static const nir_search_variable search28_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search28_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search28_1_0.value, &search28_1_1.value },
   NULL,
};
static const nir_search_expression search28 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search28_0.value, &search28_1.value },
   NULL,
};
   
static const nir_search_variable replace28 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search51_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search51_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search51_0_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search51_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search51_0_1_1_0_0.value },
   NULL,
};
static const nir_search_expression search51_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search51_0_1_1_0.value },
   NULL,
};
static const nir_search_expression search51_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search51_0_1_0.value, &search51_0_1_1.value },
   NULL,
};
static const nir_search_expression search51_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search51_0_0.value, &search51_0_1.value },
   NULL,
};

static const nir_search_variable search51_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search51_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search51_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search51_1_1_0.value },
   NULL,
};
static const nir_search_expression search51_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search51_1_0.value, &search51_1_1.value },
   NULL,
};
static const nir_search_expression search51 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search51_0.value, &search51_1.value },
   NULL,
};
   
static const nir_search_variable replace51_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace51_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace51_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace51 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace51_0.value, &replace51_1.value, &replace51_2.value },
   NULL,
};
   
static const nir_search_variable search52_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search52_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search52_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search52_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search52_0_1_1_0.value },
   NULL,
};
static const nir_search_expression search52_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search52_0_1_0.value, &search52_0_1_1.value },
   NULL,
};
static const nir_search_expression search52_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search52_0_0.value, &search52_0_1.value },
   NULL,
};

static const nir_search_variable search52_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search52_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search52_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search52_1_0.value, &search52_1_1.value },
   NULL,
};
static const nir_search_expression search52 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search52_0.value, &search52_1.value },
   NULL,
};
   
static const nir_search_variable replace52_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace52_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace52_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace52 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace52_0.value, &replace52_1.value, &replace52_2.value },
   NULL,
};
   
static const nir_search_variable search53_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search53_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search53_0_1_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search53_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search53_0_1_1_0.value },
   NULL,
};
static const nir_search_expression search53_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search53_0_1_0.value, &search53_0_1_1.value },
   NULL,
};
static const nir_search_expression search53_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search53_0_0.value, &search53_0_1.value },
   NULL,
};

static const nir_search_variable search53_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search53_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search53_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search53_1_0.value, &search53_1_1.value },
   NULL,
};
static const nir_search_expression search53 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search53_0.value, &search53_1.value },
   NULL,
};
   
static const nir_search_variable replace53_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace53_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace53_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace53 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace53_0.value, &replace53_1.value, &replace53_2.value },
   NULL,
};
   
static const nir_search_variable search54_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search54_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search54_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search54_1_0_0.value },
   NULL,
};

static const nir_search_variable search54_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search54_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search54_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search54_1_1_1_0.value },
   NULL,
};
static const nir_search_expression search54_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search54_1_1_0.value, &search54_1_1_1.value },
   NULL,
};
static const nir_search_expression search54_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search54_1_0.value, &search54_1_1.value },
   NULL,
};
static const nir_search_expression search54 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search54_0.value, &search54_1.value },
   NULL,
};
   
static const nir_search_variable replace54_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace54_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace54_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace54 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace54_0.value, &replace54_1.value, &replace54_2.value },
   NULL,
};
   
static const nir_search_variable search55_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search55_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search55_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search55_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search55_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search55_1_1_1_0.value },
   NULL,
};
static const nir_search_expression search55_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search55_1_1_0.value, &search55_1_1_1.value },
   NULL,
};
static const nir_search_expression search55_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search55_1_0.value, &search55_1_1.value },
   NULL,
};
static const nir_search_expression search55 = {
   { nir_search_value_expression, 32 },
   true,
   nir_op_fadd,
   { &search55_0.value, &search55_1.value },
   NULL,
};
   
static const nir_search_variable replace55_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace55_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace55_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace55 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace55_0.value, &replace55_1.value, &replace55_2.value },
   NULL,
};
   
static const nir_search_variable search56_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search56_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search56_1_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search56_1_1_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search56_1_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search56_1_1_1_0.value },
   NULL,
};
static const nir_search_expression search56_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search56_1_1_0.value, &search56_1_1_1.value },
   NULL,
};
static const nir_search_expression search56_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search56_1_0.value, &search56_1_1.value },
   NULL,
};
static const nir_search_expression search56 = {
   { nir_search_value_expression, 64 },
   true,
   nir_op_fadd,
   { &search56_0.value, &search56_1.value },
   NULL,
};
   
static const nir_search_variable replace56_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace56_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace56_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace56 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flrp,
   { &replace56_0.value, &replace56_1.value, &replace56_2.value },
   NULL,
};
   
static const nir_search_variable search58_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search58_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search58_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search58_0_0.value, &search58_0_1.value },
   NULL,
};

static const nir_search_variable search58_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search58 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search58_0.value, &search58_1.value },
   NULL,
};
   
static const nir_search_variable replace58_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace58_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace58_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace58 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &replace58_0.value, &replace58_1.value, &replace58_2.value },
   NULL,
};
   
static const nir_search_variable search328_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search328_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search328_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search328_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search328_1_0.value, &search328_1_1.value },
   NULL,
};
static const nir_search_expression search328 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search328_0.value, &search328_1.value },
   NULL,
};
   
static const nir_search_variable replace328_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace328_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace328 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace328_0.value, &replace328_1.value },
   NULL,
};
   
static const nir_search_variable search336_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search336_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search336_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search336_0_0.value, &search336_0_1.value },
   (is_used_once),
};

static const nir_search_variable search336_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search336 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search336_0.value, &search336_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace336_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace336_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace336_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace336_0_0.value, &replace336_0_1.value },
   NULL,
};

static const nir_search_variable replace336_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace336 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace336_0.value, &replace336_1.value },
   NULL,
};
   
static const nir_search_variable search340_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search340_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search340_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search340_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search340_1_0.value, &search340_1_1.value },
   NULL,
};
static const nir_search_expression search340 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search340_0.value, &search340_1.value },
   NULL,
};
   
static const nir_search_variable replace340_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace340_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace340_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace340_0_0.value, &replace340_0_1.value },
   NULL,
};

static const nir_search_variable replace340_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace340 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace340_0.value, &replace340_1.value },
   NULL,
};
   
static const nir_search_variable search341_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search341_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search341_1_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search341_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search341_1_0_0.value, &search341_1_0_1.value },
   NULL,
};
static const nir_search_expression search341_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search341_1_0.value },
   NULL,
};
static const nir_search_expression search341 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search341_0.value, &search341_1.value },
   NULL,
};
   
static const nir_search_variable replace341_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace341_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace341_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace341_0_1_0.value },
   NULL,
};
static const nir_search_expression replace341_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace341_0_0.value, &replace341_0_1.value },
   NULL,
};

static const nir_search_variable replace341_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace341_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace341_1_0.value },
   NULL,
};
static const nir_search_expression replace341 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace341_0.value, &replace341_1.value },
   NULL,
};
   
static const nir_search_variable search425_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search425_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search425_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search425_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search425_0_0.value, &search425_0_1.value, &search425_0_2.value },
   (is_used_once),
};

static const nir_search_variable search425_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search425 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search425_0.value, &search425_1.value },
   NULL,
};
   
static const nir_search_variable replace425_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace425_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace425_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace425_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace425_1_0.value, &replace425_1_1.value },
   NULL,
};

static const nir_search_variable replace425_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace425_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace425_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace425_2_0.value, &replace425_2_1.value },
   NULL,
};
static const nir_search_expression replace425 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace425_0.value, &replace425_1.value, &replace425_2.value },
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
   { &search328, &replace328.value, 0 },
   { &search336, &replace336.value, 0 },
   { &search340, &replace340.value, 0 },
   { &search341, &replace341.value, 0 },
   { &search425, &replace425.value, 0 },
};
   
static const nir_search_variable search18_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search18_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search18 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search18_0.value, &search18_1.value },
   NULL,
};
   
static const nir_search_variable replace18 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search22_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search22_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search22_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search22_0_0.value, &search22_0_1.value },
   NULL,
};

static const nir_search_variable search22_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search22_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search22_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search22_1_0.value, &search22_1_1.value },
   NULL,
};
static const nir_search_expression search22 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search22_0.value, &search22_1.value },
   NULL,
};
   
static const nir_search_variable replace22_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace22_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace22_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace22_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace22_1_0.value, &replace22_1_1.value },
   NULL,
};
static const nir_search_expression replace22 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace22_0.value, &replace22_1.value },
   NULL,
};
   
static const nir_search_variable search24_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search24_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search24_0_0.value },
   NULL,
};

static const nir_search_variable search24_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search24 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search24_0.value, &search24_1.value },
   NULL,
};
   
static const nir_search_constant replace24 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search25_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search25_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search25_0_0.value },
   NULL,
};

static const nir_search_variable search25_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search25_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search25_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search25_1_0.value, &search25_1_1.value },
   NULL,
};
static const nir_search_expression search25 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search25_0.value, &search25_1.value },
   NULL,
};
   
static const nir_search_variable replace25 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search26_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search26_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search26_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search26_1_0_0.value },
   NULL,
};

static const nir_search_variable search26_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search26_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search26_1_0.value, &search26_1_1.value },
   NULL,
};
static const nir_search_expression search26 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search26_0.value, &search26_1.value },
   NULL,
};
   
static const nir_search_variable replace26 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search329_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search329_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search329_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search329_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search329_1_0.value, &search329_1_1.value },
   NULL,
};
static const nir_search_expression search329 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search329_0.value, &search329_1.value },
   NULL,
};
   
static const nir_search_variable replace329_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace329_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace329 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace329_0.value, &replace329_1.value },
   NULL,
};
   
static const nir_search_variable search337_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search337_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search337_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search337_0_0.value, &search337_0_1.value },
   (is_used_once),
};

static const nir_search_variable search337_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search337 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search337_0.value, &search337_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace337_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace337_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace337_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace337_0_0.value, &replace337_0_1.value },
   NULL,
};

static const nir_search_variable replace337_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace337 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace337_0.value, &replace337_1.value },
   NULL,
};
   
static const nir_search_variable search342_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search342_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search342_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search342_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search342_1_0.value, &search342_1_1.value },
   NULL,
};
static const nir_search_expression search342 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search342_0.value, &search342_1.value },
   NULL,
};
   
static const nir_search_variable replace342_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace342_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace342_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace342_0_0.value, &replace342_0_1.value },
   NULL,
};

static const nir_search_variable replace342_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace342 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace342_0.value, &replace342_1.value },
   NULL,
};
   
static const nir_search_variable search427_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search427_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search427_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search427_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search427_0_0.value, &search427_0_1.value, &search427_0_2.value },
   (is_used_once),
};

static const nir_search_variable search427_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search427 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search427_0.value, &search427_1.value },
   NULL,
};
   
static const nir_search_variable replace427_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace427_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace427_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace427_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace427_1_0.value, &replace427_1_1.value },
   NULL,
};

static const nir_search_variable replace427_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace427_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace427_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace427_2_0.value, &replace427_2_1.value },
   NULL,
};
static const nir_search_expression replace427 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace427_0.value, &replace427_1.value, &replace427_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search18, &replace18.value, 0 },
   { &search22, &replace22.value, 0 },
   { &search24, &replace24.value, 0 },
   { &search25, &replace25.value, 0 },
   { &search26, &replace26.value, 0 },
   { &search329, &replace329.value, 0 },
   { &search337, &replace337.value, 0 },
   { &search342, &replace342.value, 0 },
   { &search427, &replace427.value, 0 },
};
   
static const nir_search_variable search19_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search19_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search19 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search19_0.value, &search19_1.value },
   NULL,
};
   
static const nir_search_variable replace19 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search20_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search20_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search20 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_usadd_4x8,
   { &search20_0.value, &search20_1.value },
   NULL,
};
   
static const nir_search_constant replace20 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};

static const struct transform nir_opt_algebraic_usadd_4x8_xforms[] = {
   { &search19, &replace19.value, 0 },
   { &search20, &replace20.value, 0 },
};
   
static const nir_search_variable search29_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search29_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search29 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search29_0.value, &search29_1.value },
   NULL,
};
   
static const nir_search_constant replace29 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search33_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search33_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search33 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search33_0.value, &search33_1.value },
   NULL,
};
   
static const nir_search_variable replace33 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search35_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search35_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
static const nir_search_expression search35 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search35_0.value, &search35_1.value },
   NULL,
};
   
static const nir_search_variable replace35_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace35 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace35_0.value },
   NULL,
};
   
static const nir_search_variable search222_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search222_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search222_0_0.value },
   NULL,
};

static const nir_search_variable search222_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search222_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search222_1_0.value },
   NULL,
};
static const nir_search_expression search222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search222_0.value, &search222_1.value },
   NULL,
};
   
static const nir_search_variable replace222_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace222_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace222_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace222_0_0.value, &replace222_0_1.value },
   NULL,
};
static const nir_search_expression replace222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace222_0.value },
   NULL,
};
   
static const nir_search_variable search279_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search279_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search279_0_0.value },
   (is_used_once),
};

static const nir_search_variable search279_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search279_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search279_1_0.value },
   (is_used_once),
};
static const nir_search_expression search279 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search279_0.value, &search279_1.value },
   NULL,
};
   
static const nir_search_variable replace279_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace279_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace279_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace279_0_0.value, &replace279_0_1.value },
   NULL,
};
static const nir_search_expression replace279 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace279_0.value },
   NULL,
};
   
static const nir_search_variable search332_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search332_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search332_0_0.value },
   NULL,
};

static const nir_search_variable search332_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search332 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search332_0.value, &search332_1.value },
   NULL,
};
   
static const nir_search_variable replace332_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace332_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace332_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace332_0_0.value, &replace332_0_1.value },
   NULL,
};
static const nir_search_expression replace332 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace332_0.value },
   NULL,
};
   
static const nir_search_variable search334_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search334_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search334_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search334_0_0.value, &search334_0_1.value },
   (is_used_once),
};

static const nir_search_variable search334_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search334 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search334_0.value, &search334_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace334_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace334_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace334_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace334_0_0.value, &replace334_0_1.value },
   NULL,
};

static const nir_search_variable replace334_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace334 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace334_0.value, &replace334_1.value },
   NULL,
};
   
static const nir_search_variable search338_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search338_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search338_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search338_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search338_1_0.value, &search338_1_1.value },
   NULL,
};
static const nir_search_expression search338 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search338_0.value, &search338_1.value },
   NULL,
};
   
static const nir_search_variable replace338_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace338_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace338_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace338_0_0.value, &replace338_0_1.value },
   NULL,
};

static const nir_search_variable replace338_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace338 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace338_0.value, &replace338_1.value },
   NULL,
};
   
static const nir_search_variable search426_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search426_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search426_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search426_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search426_0_0.value, &search426_0_1.value, &search426_0_2.value },
   (is_used_once),
};

static const nir_search_variable search426_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search426 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search426_0.value, &search426_1.value },
   NULL,
};
   
static const nir_search_variable replace426_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace426_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace426_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace426_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace426_1_0.value, &replace426_1_1.value },
   NULL,
};

static const nir_search_variable replace426_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace426_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace426_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace426_2_0.value, &replace426_2_1.value },
   NULL,
};
static const nir_search_expression replace426 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace426_0.value, &replace426_1.value, &replace426_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search29, &replace29.value, 0 },
   { &search33, &replace33.value, 0 },
   { &search35, &replace35.value, 0 },
   { &search222, &replace222.value, 0 },
   { &search279, &replace279.value, 0 },
   { &search332, &replace332.value, 0 },
   { &search334, &replace334.value, 0 },
   { &search338, &replace338.value, 0 },
   { &search426, &replace426.value, 0 },
};
   
static const nir_search_variable search31_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search31_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search31 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search31_0.value, &search31_1.value },
   NULL,
};
   
static const nir_search_constant replace31 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search32_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search32_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search32 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umul_unorm_4x8,
   { &search32_0.value, &search32_1.value },
   NULL,
};
   
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
   
static const nir_search_constant search37_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search37_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search37_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search37 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search37_0.value, &search37_1.value, &search37_2.value },
   NULL,
};
   
static const nir_search_variable replace37 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search38_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search38_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search38_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search38 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search38_0.value, &search38_1.value, &search38_2.value },
   NULL,
};
   
static const nir_search_variable replace38 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search39_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search39_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search39_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search39 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ffma,
   { &search39_0.value, &search39_1.value, &search39_2.value },
   NULL,
};
   
static const nir_search_variable replace39_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace39_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace39 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace39_0.value, &replace39_1.value },
   NULL,
};
   
static const nir_search_variable search40_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search40_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search40_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search40 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search40_0.value, &search40_1.value, &search40_2.value },
   NULL,
};
   
static const nir_search_variable replace40_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace40_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace40 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace40_0.value, &replace40_1.value },
   NULL,
};
   
static const nir_search_constant search41_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search41_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search41_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search41 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search41_0.value, &search41_1.value, &search41_2.value },
   NULL,
};
   
static const nir_search_variable replace41_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace41_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace41 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace41_0.value, &replace41_1.value },
   NULL,
};
   
static const nir_search_variable search57_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search57_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search57_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search57 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffma,
   { &search57_0.value, &search57_1.value, &search57_2.value },
   NULL,
};
   
static const nir_search_variable replace57_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace57_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace57_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace57_0_0.value, &replace57_0_1.value },
   NULL,
};

static const nir_search_variable replace57_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
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
   
static const nir_search_variable search42_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search42_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search42_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search42 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search42_0.value, &search42_1.value, &search42_2.value },
   NULL,
};
   
static const nir_search_variable replace42 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search43_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search43_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search43_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search43 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search43_0.value, &search43_1.value, &search43_2.value },
   NULL,
};
   
static const nir_search_variable replace43 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search44_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search44_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search44_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search44 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search44_0.value, &search44_1.value, &search44_2.value },
   NULL,
};
   
static const nir_search_variable replace44 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_constant search45_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search45_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search45_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search45 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search45_0.value, &search45_1.value, &search45_2.value },
   NULL,
};
   
static const nir_search_variable replace45_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace45_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace45 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace45_0.value, &replace45_1.value },
   NULL,
};
   
static const nir_search_variable search46_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search46_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search46_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search46_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search46_2_0.value },
   NULL,
};
static const nir_search_expression search46 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search46_0.value, &search46_1.value, &search46_2.value },
   NULL,
};
   
static const nir_search_variable replace46_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace46_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace46_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace46 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace46_0.value, &replace46_1.value, &replace46_2.value },
   NULL,
};
   
static const nir_search_variable search47_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search47_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search47_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search47 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flrp,
   { &search47_0.value, &search47_1.value, &search47_2.value },
   NULL,
};
   
static const nir_search_variable replace47_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace47_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace47_0_0_0.value },
   NULL,
};

static const nir_search_variable replace47_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace47_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace47_0_0.value, &replace47_0_1.value },
   NULL,
};

static const nir_search_variable replace47_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace47 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace47_0.value, &replace47_1.value },
   NULL,
};
   
static const nir_search_variable search48_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search48_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search48_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search48 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_flrp,
   { &search48_0.value, &search48_1.value, &search48_2.value },
   NULL,
};
   
static const nir_search_variable replace48_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace48_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace48_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace48_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace48_0_1_0.value, &replace48_0_1_1.value },
   NULL,
};
static const nir_search_expression replace48_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace48_0_0.value, &replace48_0_1.value },
   NULL,
};

static const nir_search_variable replace48_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace48 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace48_0.value, &replace48_1.value },
   NULL,
};
   
static const nir_search_variable search49_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search49_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search49_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search49 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_flrp,
   { &search49_0.value, &search49_1.value, &search49_2.value },
   NULL,
};
   
static const nir_search_variable replace49_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace49_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace49_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace49_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace49_0_1_0.value, &replace49_0_1_1.value },
   NULL,
};
static const nir_search_expression replace49_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace49_0_0.value, &replace49_0_1.value },
   NULL,
};

static const nir_search_variable replace49_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
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
   
static const nir_search_variable search50_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search50 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffract,
   { &search50_0.value },
   NULL,
};
   
static const nir_search_variable replace50_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace50_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace50_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace50_1_0.value },
   NULL,
};
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
   
static const nir_search_variable search59_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search59_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search59_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search59_0_3 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &search59_0_0.value, &search59_0_1.value, &search59_0_2.value, &search59_0_3.value },
   NULL,
};

static const nir_search_variable search59_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search59_0.value, &search59_1.value },
   NULL,
};
   
static const nir_search_variable replace59_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace59_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace59_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec3,
   { &replace59_0_0.value, &replace59_0_1.value, &replace59_0_2.value },
   NULL,
};

static const nir_search_variable replace59_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph,
   { &replace59_0.value, &replace59_1.value },
   NULL,
};
   
static const nir_search_variable search60_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search60_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant search60_0_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant search60_0_3 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search60_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &search60_0_0.value, &search60_0_1.value, &search60_0_2.value, &search60_0_3.value },
   NULL,
};

static const nir_search_variable search60_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search60_0.value, &search60_1.value },
   NULL,
};
   
static const nir_search_variable replace60_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace60_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace60_0.value, &replace60_1.value },
   NULL,
};
   
static const nir_search_variable search61_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search61_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search61_0_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant search61_0_3 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search61_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &search61_0_0.value, &search61_0_1.value, &search61_0_2.value, &search61_0_3.value },
   NULL,
};

static const nir_search_variable search61_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search61_0.value, &search61_1.value },
   NULL,
};
   
static const nir_search_variable replace61_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace61_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace61_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace61_0_0.value, &replace61_0_1.value },
   NULL,
};

static const nir_search_variable replace61_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot2,
   { &replace61_0.value, &replace61_1.value },
   NULL,
};
   
static const nir_search_variable search62_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search62_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search62_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search62_0_3 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search62_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &search62_0_0.value, &search62_0_1.value, &search62_0_2.value, &search62_0_3.value },
   NULL,
};

static const nir_search_variable search62_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search62_0.value, &search62_1.value },
   NULL,
};
   
static const nir_search_variable replace62_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace62_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace62_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace62_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec3,
   { &replace62_0_0.value, &replace62_0_1.value, &replace62_0_2.value },
   NULL,
};

static const nir_search_variable replace62_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &replace62_0.value, &replace62_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fdot4_xforms[] = {
   { &search59, &replace59.value, 0 },
   { &search60, &replace60.value, 0 },
   { &search61, &replace61.value, 0 },
   { &search62, &replace62.value, 0 },
};
   
static const nir_search_variable search63_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search63_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant search63_0_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search63_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec3,
   { &search63_0_0.value, &search63_0_1.value, &search63_0_2.value },
   NULL,
};

static const nir_search_variable search63_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search63 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search63_0.value, &search63_1.value },
   NULL,
};
   
static const nir_search_variable replace63_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace63_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace63 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace63_0.value, &replace63_1.value },
   NULL,
};
   
static const nir_search_variable search64_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search64_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search64_0_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search64_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec3,
   { &search64_0_0.value, &search64_0_1.value, &search64_0_2.value },
   NULL,
};

static const nir_search_variable search64_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search64_0.value, &search64_1.value },
   NULL,
};
   
static const nir_search_variable replace64_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace64_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace64_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace64_0_0.value, &replace64_0_1.value },
   NULL,
};

static const nir_search_variable replace64_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot2,
   { &replace64_0.value, &replace64_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fdot3_xforms[] = {
   { &search63, &replace63.value, 0 },
   { &search64, &replace64.value, 0 },
};
   
static const nir_search_variable search65_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search65_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search65_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search65_0_0_0.value, &search65_0_0_1.value },
   NULL,
};

static const nir_search_variable search65_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search65_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search65_0_0.value, &search65_0_1.value },
   NULL,
};

static const nir_search_variable search65_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search65_0.value, &search65_1.value },
   NULL,
};
   
static const nir_search_variable replace65_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace65_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace65_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace65_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace65_0_1_0.value, &replace65_0_1_1.value },
   NULL,
};
static const nir_search_expression replace65_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace65_0_0.value, &replace65_0_1.value },
   NULL,
};

static const nir_search_variable replace65_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace65_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace65_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace65_1_0.value, &replace65_1_1.value },
   NULL,
};
static const nir_search_expression replace65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace65_0.value, &replace65_1.value },
   NULL,
};
   
static const nir_search_variable search66_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search66_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search66_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search66_0_0.value, &search66_0_1.value },
   NULL,
};

static const nir_search_variable search66_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search66_0.value, &search66_1.value },
   NULL,
};
   
static const nir_search_variable replace66_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace66_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace66_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace66_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace66_1_0.value, &replace66_1_1.value },
   NULL,
};
static const nir_search_expression replace66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace66_0.value, &replace66_1.value },
   NULL,
};
   
static const nir_search_constant search251_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search251_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search251 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search251_0.value, &search251_1.value },
   NULL,
};
   
static const nir_search_constant replace251 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search252_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search252_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search252 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search252_0.value, &search252_1.value },
   NULL,
};
   
static const nir_search_variable replace252 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search65, &replace65.value, 0 },
   { &search66, &replace66.value, 0 },
   { &search251, &replace251.value, 0 },
   { &search252, &replace252.value, 0 },
};
   
static const nir_search_variable search67_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search67_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search67_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search67_0_0.value, &search67_0_1.value },
   NULL,
};
static const nir_search_expression search67 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search67_0.value },
   NULL,
};
   
static const nir_search_variable replace67_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace67_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace67 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace67_0.value, &replace67_1.value },
   NULL,
};
   
static const nir_search_variable search68_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search68_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search68_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search68_0_0.value, &search68_0_1.value },
   NULL,
};
static const nir_search_expression search68 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search68_0.value },
   NULL,
};
   
static const nir_search_variable replace68_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace68_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace68 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace68_0.value, &replace68_1.value },
   NULL,
};
   
static const nir_search_variable search69_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search69_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search69_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search69_0_0.value, &search69_0_1.value },
   NULL,
};
static const nir_search_expression search69 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search69_0.value },
   NULL,
};
   
static const nir_search_variable replace69_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace69_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace69_0.value, &replace69_1.value },
   NULL,
};
   
static const nir_search_variable search70_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search70_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search70_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search70_0_0.value, &search70_0_1.value },
   NULL,
};
static const nir_search_expression search70 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search70_0.value },
   NULL,
};
   
static const nir_search_variable replace70_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace70_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace70_0.value, &replace70_1.value },
   NULL,
};
   
static const nir_search_variable search71_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search71_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search71_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search71_0_0.value, &search71_0_1.value },
   NULL,
};
static const nir_search_expression search71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search71_0.value },
   NULL,
};
   
static const nir_search_variable replace71_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace71_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace71_0.value, &replace71_1.value },
   NULL,
};
   
static const nir_search_variable search72_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search72_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search72_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search72_0_0.value, &search72_0_1.value },
   NULL,
};
static const nir_search_expression search72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search72_0.value },
   NULL,
};
   
static const nir_search_variable replace72_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace72_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace72_0.value, &replace72_1.value },
   NULL,
};
   
static const nir_search_variable search73_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search73_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search73_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search73_0_0.value, &search73_0_1.value },
   NULL,
};
static const nir_search_expression search73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search73_0.value },
   NULL,
};
   
static const nir_search_variable replace73_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace73_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace73_0.value, &replace73_1.value },
   NULL,
};
   
static const nir_search_variable search74_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search74_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search74_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search74_0_0.value, &search74_0_1.value },
   NULL,
};
static const nir_search_expression search74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search74_0.value },
   NULL,
};
   
static const nir_search_variable replace74_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace74_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace74_0.value, &replace74_1.value },
   NULL,
};
   
static const nir_search_variable search75_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search75_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search75_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search75_0_0.value, &search75_0_1.value },
   NULL,
};
static const nir_search_expression search75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search75_0.value },
   NULL,
};
   
static const nir_search_variable replace75_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace75_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace75_0.value, &replace75_1.value },
   NULL,
};
   
static const nir_search_variable search76_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search76_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search76_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search76_0_0.value, &search76_0_1.value },
   NULL,
};
static const nir_search_expression search76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search76_0.value },
   NULL,
};
   
static const nir_search_variable replace76_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace76_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace76_0.value, &replace76_1.value },
   NULL,
};
   
static const nir_search_variable search244_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search244_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search244_0_0.value },
   NULL,
};
static const nir_search_expression search244 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search244_0.value },
   NULL,
};
   
static const nir_search_variable replace244 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search310_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search310_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2b,
   { &search310_0_0.value },
   NULL,
};
static const nir_search_expression search310 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search310_0.value },
   NULL,
};
   
static const nir_search_variable replace310_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace310_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace310 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace310_0.value, &replace310_1.value },
   NULL,
};
   
static const nir_search_variable search376_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search376_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search376_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search376_0_0_0.value, &search376_0_0_1.value },
   NULL,
};

static const nir_search_variable search376_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search376_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search376_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search376_0_1_0.value, &search376_0_1_1.value },
   NULL,
};
static const nir_search_expression search376_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search376_0_0.value, &search376_0_1.value },
   (is_used_once),
};
static const nir_search_expression search376 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search376_0.value },
   NULL,
};
   
static const nir_search_variable replace376_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace376_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace376_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace376_0_0.value, &replace376_0_1.value },
   NULL,
};

static const nir_search_variable replace376_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace376_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace376_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace376_1_0.value, &replace376_1_1.value },
   NULL,
};
static const nir_search_expression replace376 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace376_0.value, &replace376_1.value },
   NULL,
};
   
static const nir_search_variable search377_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search377_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search377_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search377_0_0_0.value, &search377_0_0_1.value },
   NULL,
};

static const nir_search_variable search377_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search377_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search377_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search377_0_1_0.value, &search377_0_1_1.value },
   NULL,
};
static const nir_search_expression search377_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search377_0_0.value, &search377_0_1.value },
   (is_used_once),
};
static const nir_search_expression search377 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search377_0.value },
   NULL,
};
   
static const nir_search_variable replace377_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace377_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace377_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace377_0_0.value, &replace377_0_1.value },
   NULL,
};

static const nir_search_variable replace377_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace377_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace377_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace377_1_0.value, &replace377_1_1.value },
   NULL,
};
static const nir_search_expression replace377 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace377_0.value, &replace377_1.value },
   NULL,
};
   
static const nir_search_variable search378_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search378_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search378_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search378_0_0_0.value, &search378_0_0_1.value },
   NULL,
};

static const nir_search_variable search378_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search378_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search378_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search378_0_1_0.value, &search378_0_1_1.value },
   NULL,
};
static const nir_search_expression search378_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search378_0_0.value, &search378_0_1.value },
   (is_used_once),
};
static const nir_search_expression search378 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search378_0.value },
   NULL,
};
   
static const nir_search_variable replace378_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace378_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace378_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace378_0_0.value, &replace378_0_1.value },
   NULL,
};

static const nir_search_variable replace378_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace378_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace378_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace378_1_0.value, &replace378_1_1.value },
   NULL,
};
static const nir_search_expression replace378 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace378_0.value, &replace378_1.value },
   NULL,
};
   
static const nir_search_variable search379_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search379_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search379_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search379_0_0_0.value, &search379_0_0_1.value },
   NULL,
};

static const nir_search_variable search379_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search379_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search379_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search379_0_1_0.value, &search379_0_1_1.value },
   NULL,
};
static const nir_search_expression search379_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search379_0_0.value, &search379_0_1.value },
   (is_used_once),
};
static const nir_search_expression search379 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search379_0.value },
   NULL,
};
   
static const nir_search_variable replace379_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace379_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace379_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace379_0_0.value, &replace379_0_1.value },
   NULL,
};

static const nir_search_variable replace379_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace379_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace379_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace379_1_0.value, &replace379_1_1.value },
   NULL,
};
static const nir_search_expression replace379 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace379_0.value, &replace379_1.value },
   NULL,
};
   
static const nir_search_variable search380_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search380_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search380_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search380_0_0_0.value, &search380_0_0_1.value },
   NULL,
};

static const nir_search_variable search380_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search380_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search380_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search380_0_1_0.value, &search380_0_1_1.value },
   NULL,
};
static const nir_search_expression search380_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search380_0_0.value, &search380_0_1.value },
   (is_used_once),
};
static const nir_search_expression search380 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search380_0.value },
   NULL,
};
   
static const nir_search_variable replace380_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace380_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace380_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace380_0_0.value, &replace380_0_1.value },
   NULL,
};

static const nir_search_variable replace380_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace380_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace380_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace380_1_0.value, &replace380_1_1.value },
   NULL,
};
static const nir_search_expression replace380 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace380_0.value, &replace380_1.value },
   NULL,
};
   
static const nir_search_variable search381_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search381_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search381_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search381_0_0_0.value, &search381_0_0_1.value },
   NULL,
};

static const nir_search_variable search381_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search381_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search381_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search381_0_1_0.value, &search381_0_1_1.value },
   NULL,
};
static const nir_search_expression search381_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search381_0_0.value, &search381_0_1.value },
   (is_used_once),
};
static const nir_search_expression search381 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search381_0.value },
   NULL,
};
   
static const nir_search_variable replace381_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace381_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace381_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace381_0_0.value, &replace381_0_1.value },
   NULL,
};

static const nir_search_variable replace381_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace381_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace381_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace381_1_0.value, &replace381_1_1.value },
   NULL,
};
static const nir_search_expression replace381 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace381_0.value, &replace381_1.value },
   NULL,
};
   
static const nir_search_variable search382_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search382_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search382_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search382_0_0_0.value, &search382_0_0_1.value },
   NULL,
};

static const nir_search_variable search382_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search382_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search382_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search382_0_1_0.value, &search382_0_1_1.value },
   NULL,
};
static const nir_search_expression search382_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search382_0_0.value, &search382_0_1.value },
   (is_used_once),
};
static const nir_search_expression search382 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search382_0.value },
   NULL,
};
   
static const nir_search_variable replace382_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace382_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace382_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace382_0_0.value, &replace382_0_1.value },
   NULL,
};

static const nir_search_variable replace382_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace382_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace382_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace382_1_0.value, &replace382_1_1.value },
   NULL,
};
static const nir_search_expression replace382 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace382_0.value, &replace382_1.value },
   NULL,
};
   
static const nir_search_variable search383_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search383_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search383_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search383_0_0_0.value, &search383_0_0_1.value },
   NULL,
};

static const nir_search_variable search383_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search383_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search383_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search383_0_1_0.value, &search383_0_1_1.value },
   NULL,
};
static const nir_search_expression search383_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search383_0_0.value, &search383_0_1.value },
   (is_used_once),
};
static const nir_search_expression search383 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search383_0.value },
   NULL,
};
   
static const nir_search_variable replace383_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace383_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace383_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace383_0_0.value, &replace383_0_1.value },
   NULL,
};

static const nir_search_variable replace383_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace383_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace383_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace383_1_0.value, &replace383_1_1.value },
   NULL,
};
static const nir_search_expression replace383 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace383_0.value, &replace383_1.value },
   NULL,
};
   
static const nir_search_variable search384_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search384_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search384_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search384_0_0_0.value, &search384_0_0_1.value },
   NULL,
};

static const nir_search_variable search384_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search384_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search384_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search384_0_1_0.value, &search384_0_1_1.value },
   NULL,
};
static const nir_search_expression search384_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search384_0_0.value, &search384_0_1.value },
   (is_used_once),
};
static const nir_search_expression search384 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search384_0.value },
   NULL,
};
   
static const nir_search_variable replace384_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace384_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace384_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace384_0_0.value, &replace384_0_1.value },
   NULL,
};

static const nir_search_variable replace384_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace384_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace384_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace384_1_0.value, &replace384_1_1.value },
   NULL,
};
static const nir_search_expression replace384 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace384_0.value, &replace384_1.value },
   NULL,
};
   
static const nir_search_variable search385_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search385_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search385_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search385_0_0_0.value, &search385_0_0_1.value },
   NULL,
};

static const nir_search_variable search385_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search385_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search385_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search385_0_1_0.value, &search385_0_1_1.value },
   NULL,
};
static const nir_search_expression search385_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search385_0_0.value, &search385_0_1.value },
   (is_used_once),
};
static const nir_search_expression search385 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search385_0.value },
   NULL,
};
   
static const nir_search_variable replace385_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace385_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace385_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace385_0_0.value, &replace385_0_1.value },
   NULL,
};

static const nir_search_variable replace385_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace385_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace385_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace385_1_0.value, &replace385_1_1.value },
   NULL,
};
static const nir_search_expression replace385 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace385_0.value, &replace385_1.value },
   NULL,
};
   
static const nir_search_variable search386_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search386_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search386_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search386_0_0_0.value, &search386_0_0_1.value },
   NULL,
};

static const nir_search_variable search386_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search386_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search386_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search386_0_1_0.value, &search386_0_1_1.value },
   NULL,
};
static const nir_search_expression search386_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search386_0_0.value, &search386_0_1.value },
   (is_used_once),
};
static const nir_search_expression search386 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search386_0.value },
   NULL,
};
   
static const nir_search_variable replace386_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace386_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace386_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace386_0_0.value, &replace386_0_1.value },
   NULL,
};

static const nir_search_variable replace386_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace386_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace386_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace386_1_0.value, &replace386_1_1.value },
   NULL,
};
static const nir_search_expression replace386 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace386_0.value, &replace386_1.value },
   NULL,
};
   
static const nir_search_variable search387_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search387_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search387_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search387_0_0_0.value, &search387_0_0_1.value },
   NULL,
};

static const nir_search_variable search387_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search387_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search387_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search387_0_1_0.value, &search387_0_1_1.value },
   NULL,
};
static const nir_search_expression search387_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search387_0_0.value, &search387_0_1.value },
   (is_used_once),
};
static const nir_search_expression search387 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search387_0.value },
   NULL,
};
   
static const nir_search_variable replace387_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace387_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace387_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace387_0_0.value, &replace387_0_1.value },
   NULL,
};

static const nir_search_variable replace387_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace387_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace387_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace387_1_0.value, &replace387_1_1.value },
   NULL,
};
static const nir_search_expression replace387 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace387_0.value, &replace387_1.value },
   NULL,
};
   
static const nir_search_variable search388_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search388_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search388_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search388_0_0_0.value, &search388_0_0_1.value },
   NULL,
};

static const nir_search_variable search388_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search388_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search388_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search388_0_1_0.value, &search388_0_1_1.value },
   NULL,
};
static const nir_search_expression search388_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search388_0_0.value, &search388_0_1.value },
   (is_used_once),
};
static const nir_search_expression search388 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search388_0.value },
   NULL,
};
   
static const nir_search_variable replace388_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace388_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace388_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace388_0_0.value, &replace388_0_1.value },
   NULL,
};

static const nir_search_variable replace388_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace388_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace388_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace388_1_0.value, &replace388_1_1.value },
   NULL,
};
static const nir_search_expression replace388 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace388_0.value, &replace388_1.value },
   NULL,
};
   
static const nir_search_variable search389_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search389_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search389_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search389_0_0_0.value, &search389_0_0_1.value },
   NULL,
};

static const nir_search_variable search389_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search389_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search389_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search389_0_1_0.value, &search389_0_1_1.value },
   NULL,
};
static const nir_search_expression search389_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search389_0_0.value, &search389_0_1.value },
   (is_used_once),
};
static const nir_search_expression search389 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search389_0.value },
   NULL,
};
   
static const nir_search_variable replace389_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace389_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace389_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace389_0_0.value, &replace389_0_1.value },
   NULL,
};

static const nir_search_variable replace389_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace389_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace389_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace389_1_0.value, &replace389_1_1.value },
   NULL,
};
static const nir_search_expression replace389 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace389_0.value, &replace389_1.value },
   NULL,
};
   
static const nir_search_variable search390_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search390_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search390_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search390_0_0_0.value, &search390_0_0_1.value },
   NULL,
};

static const nir_search_variable search390_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search390_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search390_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search390_0_1_0.value, &search390_0_1_1.value },
   NULL,
};
static const nir_search_expression search390_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search390_0_0.value, &search390_0_1.value },
   (is_used_once),
};
static const nir_search_expression search390 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search390_0.value },
   NULL,
};
   
static const nir_search_variable replace390_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace390_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace390_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace390_0_0.value, &replace390_0_1.value },
   NULL,
};

static const nir_search_variable replace390_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace390_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace390_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace390_1_0.value, &replace390_1_1.value },
   NULL,
};
static const nir_search_expression replace390 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace390_0.value, &replace390_1.value },
   NULL,
};
   
static const nir_search_variable search391_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search391_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search391_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search391_0_0_0.value, &search391_0_0_1.value },
   NULL,
};

static const nir_search_variable search391_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search391_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search391_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search391_0_1_0.value, &search391_0_1_1.value },
   NULL,
};
static const nir_search_expression search391_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search391_0_0.value, &search391_0_1.value },
   (is_used_once),
};
static const nir_search_expression search391 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search391_0.value },
   NULL,
};
   
static const nir_search_variable replace391_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace391_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace391_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace391_0_0.value, &replace391_0_1.value },
   NULL,
};

static const nir_search_variable replace391_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace391_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace391_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace391_1_0.value, &replace391_1_1.value },
   NULL,
};
static const nir_search_expression replace391 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace391_0.value, &replace391_1.value },
   NULL,
};
   
static const nir_search_variable search392_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search392_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search392_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search392_0_0_0.value, &search392_0_0_1.value },
   NULL,
};

static const nir_search_variable search392_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search392_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search392_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search392_0_1_0.value, &search392_0_1_1.value },
   NULL,
};
static const nir_search_expression search392_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search392_0_0.value, &search392_0_1.value },
   (is_used_once),
};
static const nir_search_expression search392 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search392_0.value },
   NULL,
};
   
static const nir_search_variable replace392_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace392_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace392_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace392_0_0.value, &replace392_0_1.value },
   NULL,
};

static const nir_search_variable replace392_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace392_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace392_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace392_1_0.value, &replace392_1_1.value },
   NULL,
};
static const nir_search_expression replace392 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace392_0.value, &replace392_1.value },
   NULL,
};
   
static const nir_search_variable search393_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search393_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search393_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search393_0_0_0.value, &search393_0_0_1.value },
   NULL,
};

static const nir_search_variable search393_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search393_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search393_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search393_0_1_0.value, &search393_0_1_1.value },
   NULL,
};
static const nir_search_expression search393_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search393_0_0.value, &search393_0_1.value },
   (is_used_once),
};
static const nir_search_expression search393 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search393_0.value },
   NULL,
};
   
static const nir_search_variable replace393_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace393_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace393_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace393_0_0.value, &replace393_0_1.value },
   NULL,
};

static const nir_search_variable replace393_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace393_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace393_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace393_1_0.value, &replace393_1_1.value },
   NULL,
};
static const nir_search_expression replace393 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace393_0.value, &replace393_1.value },
   NULL,
};
   
static const nir_search_variable search394_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search394_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search394_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search394_0_0_0.value, &search394_0_0_1.value },
   NULL,
};

static const nir_search_variable search394_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search394_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search394_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search394_0_1_0.value, &search394_0_1_1.value },
   NULL,
};
static const nir_search_expression search394_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search394_0_0.value, &search394_0_1.value },
   (is_used_once),
};
static const nir_search_expression search394 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search394_0.value },
   NULL,
};
   
static const nir_search_variable replace394_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace394_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace394_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace394_0_0.value, &replace394_0_1.value },
   NULL,
};

static const nir_search_variable replace394_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace394_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace394_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace394_1_0.value, &replace394_1_1.value },
   NULL,
};
static const nir_search_expression replace394 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace394_0.value, &replace394_1.value },
   NULL,
};
   
static const nir_search_variable search395_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search395_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search395_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search395_0_0_0.value, &search395_0_0_1.value },
   NULL,
};

static const nir_search_variable search395_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search395_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search395_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search395_0_1_0.value, &search395_0_1_1.value },
   NULL,
};
static const nir_search_expression search395_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search395_0_0.value, &search395_0_1.value },
   (is_used_once),
};
static const nir_search_expression search395 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search395_0.value },
   NULL,
};
   
static const nir_search_variable replace395_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace395_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace395_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace395_0_0.value, &replace395_0_1.value },
   NULL,
};

static const nir_search_variable replace395_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace395_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace395_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace395_1_0.value, &replace395_1_1.value },
   NULL,
};
static const nir_search_expression replace395 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace395_0.value, &replace395_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_inot_xforms[] = {
   { &search67, &replace67.value, 0 },
   { &search68, &replace68.value, 0 },
   { &search69, &replace69.value, 0 },
   { &search70, &replace70.value, 0 },
   { &search71, &replace71.value, 0 },
   { &search72, &replace72.value, 0 },
   { &search73, &replace73.value, 0 },
   { &search74, &replace74.value, 0 },
   { &search75, &replace75.value, 0 },
   { &search76, &replace76.value, 0 },
   { &search244, &replace244.value, 0 },
   { &search310, &replace310.value, 0 },
   { &search376, &replace376.value, 0 },
   { &search377, &replace377.value, 0 },
   { &search378, &replace378.value, 0 },
   { &search379, &replace379.value, 0 },
   { &search380, &replace380.value, 0 },
   { &search381, &replace381.value, 0 },
   { &search382, &replace382.value, 0 },
   { &search383, &replace383.value, 0 },
   { &search384, &replace384.value, 0 },
   { &search385, &replace385.value, 0 },
   { &search386, &replace386.value, 0 },
   { &search387, &replace387.value, 0 },
   { &search388, &replace388.value, 0 },
   { &search389, &replace389.value, 0 },
   { &search390, &replace390.value, 0 },
   { &search391, &replace391.value, 0 },
   { &search392, &replace392.value, 0 },
   { &search393, &replace393.value, 0 },
   { &search394, &replace394.value, 0 },
   { &search395, &replace395.value, 0 },
};
   
static const nir_search_constant search77_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search77_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search77_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search77_1_0.value },
   NULL,
};
static const nir_search_expression search77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search77_0.value, &search77_1.value },
   NULL,
};
   
static const nir_search_variable replace77_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace77_0.value },
   NULL,
};
   
static const nir_search_variable search78_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search78_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search78_0_0_0.value },
   NULL,
};
static const nir_search_expression search78_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search78_0_0.value },
   NULL,
};

static const nir_search_constant search78_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search78 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search78_0.value, &search78_1.value },
   NULL,
};
   
static const nir_search_variable replace78_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace78 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace78_0.value },
   NULL,
};
   
static const nir_search_variable search80_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search80_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search80_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search80_0_0.value, &search80_0_1.value },
   NULL,
};

static const nir_search_variable search80_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search80 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search80_0.value, &search80_1.value },
   NULL,
};
   
static const nir_search_variable replace80_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace80_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace80 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace80_0.value, &replace80_1.value },
   NULL,
};
   
static const nir_search_variable search85_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search85_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search85_0_0_0_0.value },
   NULL,
};
static const nir_search_expression search85_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search85_0_0_0.value },
   NULL,
};

static const nir_search_variable search85_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search85_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search85_0_0.value, &search85_0_1.value },
   NULL,
};

static const nir_search_constant search85_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search85_0.value, &search85_1.value },
   NULL,
};
   
static const nir_search_variable replace85_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace85_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace85_0_0.value },
   NULL,
};

static const nir_search_variable replace85_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace85_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace85_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace85_1_0.value, &replace85_1_1.value },
   NULL,
};
static const nir_search_expression replace85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace85_0.value, &replace85_1.value },
   NULL,
};
   
static const nir_search_variable search98_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search98_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search98_0_0_0.value },
   NULL,
};
static const nir_search_expression search98_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search98_0_0.value },
   NULL,
};

static const nir_search_constant search98_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search98_0.value, &search98_1.value },
   NULL,
};
   
static const nir_search_variable replace98_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace98_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace98_0.value, &replace98_1.value },
   NULL,
};
   
static const nir_search_variable search173_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search173_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search173_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search173_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search173_1_0.value, &search173_1_1.value },
   NULL,
};
static const nir_search_expression search173 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search173_0.value, &search173_1.value },
   NULL,
};
   
static const nir_search_constant replace173 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search174_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search174_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search174_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search174_0_0.value, &search174_0_1.value },
   NULL,
};

static const nir_search_variable search174_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search174 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search174_0.value, &search174_1.value },
   NULL,
};
   
static const nir_search_constant replace174 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search177_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search177_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search177_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search177_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search177_1_0.value, &search177_1_1.value },
   NULL,
};
static const nir_search_expression search177 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search177_0.value, &search177_1.value },
   NULL,
};
   
static const nir_search_variable replace177_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace177_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace177_0.value, &replace177_1.value },
   NULL,
};
   
static const nir_search_variable search178_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search178_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search178_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search178_0_0.value, &search178_0_1.value },
   NULL,
};

static const nir_search_variable search178_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search178 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search178_0.value, &search178_1.value },
   NULL,
};
   
static const nir_search_variable replace178_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace178_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace178_0.value, &replace178_1.value },
   NULL,
};
   
static const nir_search_variable search407_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search407_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search407_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search407_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search407_0_0.value, &search407_0_1.value, &search407_0_2.value },
   NULL,
};

static const nir_search_variable search407_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search407 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search407_0.value, &search407_1.value },
   NULL,
};
   
static const nir_search_variable replace407_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace407_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace407_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace407_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace407_1_0.value, &replace407_1_1.value },
   NULL,
};

static const nir_search_variable replace407_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace407_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace407_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace407_2_0.value, &replace407_2_1.value },
   NULL,
};
static const nir_search_expression replace407 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace407_0.value, &replace407_1.value, &replace407_2.value },
   NULL,
};
   
static const nir_search_variable search408_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search408_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search408_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search408_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search408_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search408_1_0.value, &search408_1_1.value, &search408_1_2.value },
   NULL,
};
static const nir_search_expression search408 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search408_0.value, &search408_1.value },
   NULL,
};
   
static const nir_search_variable replace408_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace408_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace408_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace408_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace408_1_0.value, &replace408_1_1.value },
   NULL,
};

static const nir_search_variable replace408_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace408_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace408_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace408_2_0.value, &replace408_2_1.value },
   NULL,
};
static const nir_search_expression replace408 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace408_0.value, &replace408_1.value, &replace408_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search77, &replace77.value, 0 },
   { &search78, &replace78.value, 0 },
   { &search80, &replace80.value, 0 },
   { &search85, &replace85.value, 0 },
   { &search98, &replace98.value, 0 },
   { &search173, &replace173.value, 0 },
   { &search174, &replace174.value, 0 },
   { &search177, &replace177.value, 0 },
   { &search178, &replace178.value, 0 },
   { &search407, &replace407.value, 0 },
   { &search408, &replace408.value, 0 },
};
   
static const nir_search_variable search79_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search79_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search79_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search79_0_0.value, &search79_0_1.value },
   NULL,
};

static const nir_search_variable search79_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search79 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flt,
   { &search79_0.value, &search79_1.value },
   NULL,
};
   
static const nir_search_variable replace79_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace79_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace79 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace79_0.value, &replace79_1.value },
   NULL,
};
   
static const nir_search_constant search91_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search91_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search91_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search91_1_0.value },
   NULL,
};
static const nir_search_expression search91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search91_0.value, &search91_1.value },
   NULL,
};
   
static const nir_search_variable replace91_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace91_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace91_0.value, &replace91_1.value },
   NULL,
};
   
static const nir_search_variable search97_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search97_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search97_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search97_0_1_0.value },
   NULL,
};
static const nir_search_expression search97_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search97_0_0.value, &search97_0_1.value },
   (is_used_once),
};

static const nir_search_constant search97_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search97_0.value, &search97_1.value },
   NULL,
};
   
static const nir_search_variable replace97_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace97_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace97_0.value, &replace97_1.value },
   NULL,
};
   
static const nir_search_variable search171_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search171_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search171_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search171_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search171_1_0.value, &search171_1_1.value },
   NULL,
};
static const nir_search_expression search171 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flt,
   { &search171_0.value, &search171_1.value },
   NULL,
};
   
static const nir_search_variable replace171_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace171_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace171_0.value, &replace171_1.value },
   NULL,
};
   
static const nir_search_variable search172_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search172_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search172_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search172_0_0.value, &search172_0_1.value },
   NULL,
};

static const nir_search_variable search172_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search172 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flt,
   { &search172_0.value, &search172_1.value },
   NULL,
};
   
static const nir_search_variable replace172_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace172_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace172 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace172_0.value, &replace172_1.value },
   NULL,
};
   
static const nir_search_variable search175_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search175_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search175_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search175_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search175_1_0.value, &search175_1_1.value },
   NULL,
};
static const nir_search_expression search175 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flt,
   { &search175_0.value, &search175_1.value },
   NULL,
};
   
static const nir_search_constant replace175 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search176_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search176_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search176_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search176_0_0.value, &search176_0_1.value },
   NULL,
};

static const nir_search_variable search176_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search176 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flt,
   { &search176_0.value, &search176_1.value },
   NULL,
};
   
static const nir_search_constant replace176 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search226_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search226_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search226_0_0_0.value },
   NULL,
};
static const nir_search_expression search226_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search226_0_0.value },
   NULL,
};

static const nir_search_constant search226_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search226_0.value, &search226_1.value },
   NULL,
};
   
static const nir_search_variable replace226 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_constant search227_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search227_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search227_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search227_0_1_0.value },
   NULL,
};
static const nir_search_expression search227_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search227_0_0.value, &search227_0_1.value },
   NULL,
};

static const nir_search_constant search227_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search227_0.value, &search227_1.value },
   NULL,
};
   
static const nir_search_variable replace227 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search405_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search405_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search405_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search405_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search405_0_0.value, &search405_0_1.value, &search405_0_2.value },
   NULL,
};

static const nir_search_variable search405_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search405 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search405_0.value, &search405_1.value },
   NULL,
};
   
static const nir_search_variable replace405_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace405_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace405_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace405_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace405_1_0.value, &replace405_1_1.value },
   NULL,
};

static const nir_search_variable replace405_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace405_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace405_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace405_2_0.value, &replace405_2_1.value },
   NULL,
};
static const nir_search_expression replace405 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace405_0.value, &replace405_1.value, &replace405_2.value },
   NULL,
};
   
static const nir_search_variable search406_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search406_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search406_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search406_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search406_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search406_1_0.value, &search406_1_1.value, &search406_1_2.value },
   NULL,
};
static const nir_search_expression search406 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search406_0.value, &search406_1.value },
   NULL,
};
   
static const nir_search_variable replace406_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace406_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace406_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace406_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace406_1_0.value, &replace406_1_1.value },
   NULL,
};

static const nir_search_variable replace406_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace406_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace406_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace406_2_0.value, &replace406_2_1.value },
   NULL,
};
static const nir_search_expression replace406 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace406_0.value, &replace406_1.value, &replace406_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search79, &replace79.value, 0 },
   { &search91, &replace91.value, 0 },
   { &search97, &replace97.value, 0 },
   { &search171, &replace171.value, 0 },
   { &search172, &replace172.value, 0 },
   { &search175, &replace175.value, 0 },
   { &search176, &replace176.value, 0 },
   { &search226, &replace226.value, 0 },
   { &search227, &replace227.value, 0 },
   { &search405, &replace405.value, 0 },
   { &search406, &replace406.value, 0 },
};
   
static const nir_search_variable search81_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search81_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search81_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search81_0_0.value, &search81_0_1.value },
   NULL,
};

static const nir_search_variable search81_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search81 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_feq,
   { &search81_0.value, &search81_1.value },
   NULL,
};
   
static const nir_search_variable replace81_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace81_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace81 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace81_0.value, &replace81_1.value },
   NULL,
};
   
static const nir_search_variable search86_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search86_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search86_0_0_0_0.value },
   NULL,
};
static const nir_search_expression search86_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search86_0_0_0.value },
   NULL,
};

static const nir_search_variable search86_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search86_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search86_0_0.value, &search86_0_1.value },
   NULL,
};

static const nir_search_constant search86_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search86_0.value, &search86_1.value },
   NULL,
};
   
static const nir_search_variable replace86_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace86_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace86_0_0.value },
   NULL,
};

static const nir_search_variable replace86_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace86_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace86_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace86_1_0.value, &replace86_1_1.value },
   NULL,
};
static const nir_search_expression replace86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace86_0.value, &replace86_1.value },
   NULL,
};
   
static const nir_search_variable search87_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search87_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search87_0_0.value },
   NULL,
};

static const nir_search_constant search87_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search87_0.value, &search87_1.value },
   NULL,
};
   
static const nir_search_variable replace87_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace87_0.value },
   NULL,
};
   
static const nir_search_variable search220_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search220_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search220_0_0.value },
   NULL,
};

static const nir_search_variable search220_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search220_0.value, &search220_1.value },
   NULL,
};
   
static const nir_search_variable replace220_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace220_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace220_0.value, &replace220_1.value },
   NULL,
};
   
static const nir_search_variable search409_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search409_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search409_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search409_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search409_0_0.value, &search409_0_1.value, &search409_0_2.value },
   NULL,
};

static const nir_search_variable search409_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search409 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search409_0.value, &search409_1.value },
   NULL,
};
   
static const nir_search_variable replace409_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace409_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace409_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace409_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace409_1_0.value, &replace409_1_1.value },
   NULL,
};

static const nir_search_variable replace409_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace409_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace409_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace409_2_0.value, &replace409_2_1.value },
   NULL,
};
static const nir_search_expression replace409 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace409_0.value, &replace409_1.value, &replace409_2.value },
   NULL,
};
   
static const nir_search_variable search410_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search410_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search410_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search410_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search410_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search410_1_0.value, &search410_1_1.value, &search410_1_2.value },
   NULL,
};
static const nir_search_expression search410 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search410_0.value, &search410_1.value },
   NULL,
};
   
static const nir_search_variable replace410_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace410_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace410_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace410_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace410_1_0.value, &replace410_1_1.value },
   NULL,
};

static const nir_search_variable replace410_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace410_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace410_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace410_2_0.value, &replace410_2_1.value },
   NULL,
};
static const nir_search_expression replace410 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace410_0.value, &replace410_1.value, &replace410_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search81, &replace81.value, 0 },
   { &search86, &replace86.value, 0 },
   { &search87, &replace87.value, 0 },
   { &search220, &replace220.value, 0 },
   { &search409, &replace409.value, 0 },
   { &search410, &replace410.value, 0 },
};
   
static const nir_search_variable search82_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search82_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search82_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search82_0_0.value, &search82_0_1.value },
   NULL,
};

static const nir_search_variable search82_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search82 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fne,
   { &search82_0.value, &search82_1.value },
   NULL,
};
   
static const nir_search_variable replace82_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace82_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace82 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace82_0.value, &replace82_1.value },
   NULL,
};
   
static const nir_search_variable search88_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search88_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search88_0_0.value },
   NULL,
};

static const nir_search_constant search88_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search88_0.value, &search88_1.value },
   NULL,
};
   
static const nir_search_variable replace88 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search219_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search219_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search219_0_0.value },
   NULL,
};

static const nir_search_variable search219_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search219_0.value, &search219_1.value },
   NULL,
};
   
static const nir_search_variable replace219_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace219_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace219_0.value, &replace219_1.value },
   NULL,
};
   
static const nir_search_variable search411_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search411_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search411_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search411_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search411_0_0.value, &search411_0_1.value, &search411_0_2.value },
   NULL,
};

static const nir_search_variable search411_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search411 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search411_0.value, &search411_1.value },
   NULL,
};
   
static const nir_search_variable replace411_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace411_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace411_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace411_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace411_1_0.value, &replace411_1_1.value },
   NULL,
};

static const nir_search_variable replace411_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace411_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace411_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace411_2_0.value, &replace411_2_1.value },
   NULL,
};
static const nir_search_expression replace411 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace411_0.value, &replace411_1.value, &replace411_2.value },
   NULL,
};
   
static const nir_search_variable search412_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search412_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search412_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search412_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search412_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search412_1_0.value, &search412_1_1.value, &search412_1_2.value },
   NULL,
};
static const nir_search_expression search412 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search412_0.value, &search412_1.value },
   NULL,
};
   
static const nir_search_variable replace412_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace412_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace412_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace412_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace412_1_0.value, &replace412_1_1.value },
   NULL,
};

static const nir_search_variable replace412_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace412_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace412_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace412_2_0.value, &replace412_2_1.value },
   NULL,
};
static const nir_search_expression replace412 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace412_0.value, &replace412_1.value, &replace412_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search82, &replace82.value, 0 },
   { &search88, &replace88.value, 0 },
   { &search219, &replace219.value, 0 },
   { &search411, &replace411.value, 0 },
   { &search412, &replace412.value, 0 },
};
   
static const nir_search_variable search83_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search83_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search83_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search83_0_0.value, &search83_0_1.value },
   NULL,
};

static const nir_search_variable search83_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search83_0.value, &search83_1.value },
   NULL,
};
   
static const nir_search_variable replace83_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace83_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace83 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace83_0.value, &replace83_1.value },
   NULL,
};
   
static const nir_search_variable search89_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search89_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search89_0_0.value },
   NULL,
};

static const nir_search_constant search89_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search89_0.value, &search89_1.value },
   NULL,
};
   
static const nir_search_variable replace89_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace89_0.value },
   NULL,
};
   
static const nir_search_variable search230_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search230_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search230_0.value, &search230_1.value },
   NULL,
};
   
static const nir_search_constant replace230 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search286_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search286_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search286 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search286_0.value, &search286_1.value },
   NULL,
};
   
static const nir_search_variable replace286 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search289_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search289_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search289 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search289_0.value, &search289_1.value },
   (is_not_used_by_if),
};
   
static const nir_search_variable replace289_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace289 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace289_0.value },
   NULL,
};
   
static const nir_search_variable search417_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search417_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search417_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search417_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search417_0_0.value, &search417_0_1.value, &search417_0_2.value },
   NULL,
};

static const nir_search_variable search417_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search417 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search417_0.value, &search417_1.value },
   NULL,
};
   
static const nir_search_variable replace417_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace417_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace417_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace417_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace417_1_0.value, &replace417_1_1.value },
   NULL,
};

static const nir_search_variable replace417_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace417_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace417_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace417_2_0.value, &replace417_2_1.value },
   NULL,
};
static const nir_search_expression replace417 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace417_0.value, &replace417_1.value, &replace417_2.value },
   NULL,
};
   
static const nir_search_variable search418_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search418_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search418_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search418_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search418_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search418_1_0.value, &search418_1_1.value, &search418_1_2.value },
   NULL,
};
static const nir_search_expression search418 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search418_0.value, &search418_1.value },
   NULL,
};
   
static const nir_search_variable replace418_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace418_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace418_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace418_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace418_1_0.value, &replace418_1_1.value },
   NULL,
};

static const nir_search_variable replace418_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace418_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace418_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace418_2_0.value, &replace418_2_1.value },
   NULL,
};
static const nir_search_expression replace418 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace418_0.value, &replace418_1.value, &replace418_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search83, &replace83.value, 0 },
   { &search89, &replace89.value, 0 },
   { &search230, &replace230.value, 0 },
   { &search286, &replace286.value, 0 },
   { &search289, &replace289.value, 0 },
   { &search417, &replace417.value, 0 },
   { &search418, &replace418.value, 0 },
};
   
static const nir_search_variable search84_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search84_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search84_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search84_0_0.value, &search84_0_1.value },
   NULL,
};

static const nir_search_variable search84_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search84_0.value, &search84_1.value },
   NULL,
};
   
static const nir_search_variable replace84_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace84_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace84_0.value, &replace84_1.value },
   NULL,
};
   
static const nir_search_variable search90_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search90_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search90_0_0.value },
   NULL,
};

static const nir_search_constant search90_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search90_0.value, &search90_1.value },
   NULL,
};
   
static const nir_search_variable replace90 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search231_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search231_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search231_0.value, &search231_1.value },
   NULL,
};
   
static const nir_search_constant replace231 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search287_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search287_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search287 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search287_0.value, &search287_1.value },
   (is_not_used_by_if),
};
   
static const nir_search_variable replace287_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace287 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace287_0.value },
   NULL,
};
   
static const nir_search_variable search288_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search288_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search288 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search288_0.value, &search288_1.value },
   NULL,
};
   
static const nir_search_variable replace288 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search419_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search419_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search419_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search419_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search419_0_0.value, &search419_0_1.value, &search419_0_2.value },
   NULL,
};

static const nir_search_variable search419_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search419 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search419_0.value, &search419_1.value },
   NULL,
};
   
static const nir_search_variable replace419_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace419_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace419_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace419_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace419_1_0.value, &replace419_1_1.value },
   NULL,
};

static const nir_search_variable replace419_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace419_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace419_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace419_2_0.value, &replace419_2_1.value },
   NULL,
};
static const nir_search_expression replace419 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace419_0.value, &replace419_1.value, &replace419_2.value },
   NULL,
};
   
static const nir_search_variable search420_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search420_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search420_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search420_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search420_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search420_1_0.value, &search420_1_1.value, &search420_1_2.value },
   NULL,
};
static const nir_search_expression search420 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search420_0.value, &search420_1.value },
   NULL,
};
   
static const nir_search_variable replace420_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace420_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace420_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace420_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace420_1_0.value, &replace420_1_1.value },
   NULL,
};

static const nir_search_variable replace420_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace420_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace420_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace420_2_0.value, &replace420_2_1.value },
   NULL,
};
static const nir_search_expression replace420 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace420_0.value, &replace420_1.value, &replace420_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search84, &replace84.value, 0 },
   { &search90, &replace90.value, 0 },
   { &search231, &replace231.value, 0 },
   { &search287, &replace287.value, 0 },
   { &search288, &replace288.value, 0 },
   { &search419, &replace419.value, 0 },
   { &search420, &replace420.value, 0 },
};
   
static const nir_search_variable search92_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search92_0_0.value },
   (is_used_once),
};

static const nir_search_variable search92_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search92_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search92_1_0.value },
   NULL,
};
static const nir_search_expression search92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search92_0.value, &search92_1.value },
   NULL,
};
   
static const nir_search_variable replace92_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace92_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace92_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace92_0_0.value, &replace92_0_1.value },
   NULL,
};
static const nir_search_expression replace92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace92_0.value },
   NULL,
};
   
static const nir_search_variable search93_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search93_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search93_0_0_0.value },
   (is_used_once),
};
static const nir_search_expression search93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search93_0_0.value },
   (is_used_once),
};

static const nir_search_variable search93_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search93_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search93_1_0_0.value },
   NULL,
};
static const nir_search_expression search93_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search93_1_0.value },
   NULL,
};
static const nir_search_expression search93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search93_0.value, &search93_1.value },
   NULL,
};
   
static const nir_search_variable replace93_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace93_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace93_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace93_0_0_0.value, &replace93_0_0_1.value },
   NULL,
};
static const nir_search_expression replace93_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace93_0_0.value },
   NULL,
};
static const nir_search_expression replace93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace93_0.value },
   NULL,
};
   
static const nir_search_variable search107_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search107_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search107 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search107_0.value, &search107_1.value },
   NULL,
};
   
static const nir_search_variable replace107 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search112_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search112_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search112_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search112_1_0.value },
   NULL,
};
static const nir_search_expression search112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search112_0.value, &search112_1.value },
   NULL,
};
   
static const nir_search_variable replace112_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace112_0.value },
   NULL,
};
   
static const nir_search_variable search120_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search120_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search120_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search120_1_0_0.value },
   NULL,
};
static const nir_search_expression search120_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search120_1_0.value },
   NULL,
};
static const nir_search_expression search120 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search120_0.value, &search120_1.value },
   NULL,
};
   
static const nir_search_variable replace120 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search122_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search122_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search122_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search122_1_0.value },
   NULL,
};
static const nir_search_expression search122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search122_0.value, &search122_1.value },
   NULL,
};
   
static const nir_search_variable replace122_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace122_0.value },
   NULL,
};
   
static const nir_search_variable search124_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search124_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search124_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search124_1_0.value },
   NULL,
};
static const nir_search_expression search124 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search124_0.value, &search124_1.value },
   NULL,
};
   
static const nir_search_variable replace124_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace124 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace124_0.value },
   NULL,
};
   
static const nir_search_variable search127_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search127_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search127_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search127_0_0.value, &search127_0_1.value },
   NULL,
};

static const nir_search_constant search127_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search127 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmax,
   { &search127_0.value, &search127_1.value },
   NULL,
};
   
static const nir_search_variable replace127_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace127 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace127_0.value },
   NULL,
};
   
static const nir_search_variable search133_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search133_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search133_0_0.value },
   NULL,
};

static const nir_search_variable search133_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_zero_to_one),
};
static const nir_search_expression search133 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search133_0.value, &search133_1.value },
   NULL,
};
   
static const nir_search_variable replace133_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace133_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace133_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace133_0_0.value, &replace133_0_1.value },
   NULL,
};
static const nir_search_expression replace133 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace133_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmax_xforms[] = {
   { &search92, &replace92.value, 0 },
   { &search93, &replace93.value, 0 },
   { &search107, &replace107.value, 0 },
   { &search112, &replace112.value, 0 },
   { &search120, &replace120.value, 0 },
   { &search122, &replace122.value, 0 },
   { &search124, &replace124.value, 0 },
   { &search127, &replace127.value, 9 },
   { &search133, &replace133.value, 0 },
};
   
static const nir_search_variable search94_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search94_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search94_0_0.value },
   (is_used_once),
};

static const nir_search_variable search94_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search94_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search94_1_0.value },
   NULL,
};
static const nir_search_expression search94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search94_0.value, &search94_1.value },
   NULL,
};
   
static const nir_search_variable replace94_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace94_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace94_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace94_0_0.value, &replace94_0_1.value },
   NULL,
};
static const nir_search_expression replace94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace94_0.value },
   NULL,
};
   
static const nir_search_variable search95_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search95_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search95_0_0_0.value },
   (is_used_once),
};
static const nir_search_expression search95_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search95_0_0.value },
   (is_used_once),
};

static const nir_search_variable search95_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search95_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search95_1_0_0.value },
   NULL,
};
static const nir_search_expression search95_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search95_1_0.value },
   NULL,
};
static const nir_search_expression search95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search95_0.value, &search95_1.value },
   NULL,
};
   
static const nir_search_variable replace95_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace95_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace95_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace95_0_0_0.value, &replace95_0_0_1.value },
   NULL,
};
static const nir_search_expression replace95_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace95_0_0.value },
   NULL,
};
static const nir_search_expression replace95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace95_0.value },
   NULL,
};
   
static const nir_search_variable search96_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search96_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search96_0_0.value },
   NULL,
};

static const nir_search_variable search96_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search96_0.value, &search96_1.value },
   NULL,
};
   
static const nir_search_variable replace96_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace96_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace96_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace96_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace96_1_0.value, &replace96_1_1.value },
   NULL,
};

static const nir_search_variable replace96_2_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace96_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace96_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace96_2_0.value, &replace96_2_1.value },
   NULL,
};
static const nir_search_expression replace96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace96_0.value, &replace96_1.value, &replace96_2.value },
   NULL,
};
   
static const nir_search_variable search106_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search106_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search106 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search106_0.value, &search106_1.value },
   NULL,
};
   
static const nir_search_variable replace106 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search114_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search114_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search114_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search114_1_0.value },
   NULL,
};
static const nir_search_expression search114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search114_0.value, &search114_1.value },
   NULL,
};
   
static const nir_search_variable replace114_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace114_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace114_0_0.value },
   NULL,
};
static const nir_search_expression replace114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace114_0.value },
   NULL,
};
   
static const nir_search_variable search116_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search116_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search116_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search116_1_0_0.value },
   NULL,
};
static const nir_search_expression search116_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search116_1_0.value },
   NULL,
};
static const nir_search_expression search116 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search116_0.value, &search116_1.value },
   NULL,
};
   
static const nir_search_variable replace116_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace116_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace116_0_0.value },
   NULL,
};
static const nir_search_expression replace116 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace116_0.value },
   NULL,
};
   
static const nir_search_variable search118_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search118_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search118_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search118_1_0.value },
   NULL,
};
static const nir_search_expression search118 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search118_0.value, &search118_1.value },
   NULL,
};
   
static const nir_search_variable replace118 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search126_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search126_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search126_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search126_0_0.value, &search126_0_1.value },
   NULL,
};

static const nir_search_constant search126_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search126 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmin,
   { &search126_0.value, &search126_1.value },
   NULL,
};
   
static const nir_search_variable replace126_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace126 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace126_0.value },
   NULL,
};
   
static const nir_search_variable search130_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search130_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search130_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search130_0_0_0_0.value, &search130_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search130_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search130_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search130_0_0_0.value, &search130_0_0_1.value },
   NULL,
};

static const nir_search_variable search130_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search130_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search130_0_0.value, &search130_0_1.value },
   NULL,
};

static const nir_search_variable search130_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search130 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search130_0.value, &search130_1.value },
   NULL,
};
   
static const nir_search_variable replace130_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace130_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace130_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace130_0_0.value, &replace130_0_1.value },
   NULL,
};

static const nir_search_variable replace130_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace130 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace130_0.value, &replace130_1.value },
   NULL,
};
   
static const nir_search_variable search134_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search134_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search134_0_0.value },
   NULL,
};

static const nir_search_variable search134_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_zero_to_one),
};
static const nir_search_expression search134 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search134_0.value, &search134_1.value },
   NULL,
};
   
static const nir_search_variable replace134_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace134_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace134_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace134_0_0.value, &replace134_0_1.value },
   NULL,
};
static const nir_search_expression replace134 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace134_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search94, &replace94.value, 0 },
   { &search95, &replace95.value, 0 },
   { &search96, &replace96.value, 0 },
   { &search106, &replace106.value, 0 },
   { &search114, &replace114.value, 0 },
   { &search116, &replace116.value, 0 },
   { &search118, &replace118.value, 0 },
   { &search126, &replace126.value, 9 },
   { &search130, &replace130.value, 0 },
   { &search134, &replace134.value, 0 },
};
   
static const nir_search_variable search99_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search99_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search99_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search99_0_0.value, &search99_0_1.value },
   NULL,
};

static const nir_search_variable search99_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search99_2 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search99 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_bcsel,
   { &search99_0.value, &search99_1.value, &search99_2.value },
   NULL,
};
   
static const nir_search_variable replace99_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace99_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace99_0.value, &replace99_1.value },
   NULL,
};
   
static const nir_search_variable search100_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search100_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search100_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search100_0_0.value, &search100_0_1.value },
   NULL,
};

static const nir_search_variable search100_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search100_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search100 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_bcsel,
   { &search100_0.value, &search100_1.value, &search100_2.value },
   NULL,
};
   
static const nir_search_variable replace100_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace100_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace100_0.value, &replace100_1.value },
   NULL,
};
   
static const nir_search_variable search101_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search101_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search101_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search101_0_0.value, &search101_0_1.value },
   NULL,
};

static const nir_search_variable search101_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search101_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search101 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_bcsel,
   { &search101_0.value, &search101_1.value, &search101_2.value },
   NULL,
};
   
static const nir_search_variable replace101_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace101_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace101_0.value, &replace101_1.value },
   NULL,
};
   
static const nir_search_variable search102_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search102_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search102_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search102_0_0.value, &search102_0_1.value },
   NULL,
};

static const nir_search_variable search102_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search102_2 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search102 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_bcsel,
   { &search102_0.value, &search102_1.value, &search102_2.value },
   NULL,
};
   
static const nir_search_variable replace102_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace102_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace102_0.value, &replace102_1.value },
   NULL,
};
   
static const nir_search_variable search103_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search103_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search103_0_0.value },
   NULL,
};

static const nir_search_variable search103_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search103_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search103_0.value, &search103_1.value, &search103_2.value },
   NULL,
};
   
static const nir_search_variable replace103_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace103_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace103_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace103_0.value, &replace103_1.value, &replace103_2.value },
   NULL,
};
   
static const nir_search_variable search104_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search104_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search104_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search104_1_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search104_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search104_1_0.value, &search104_1_1.value, &search104_1_2.value },
   NULL,
};

static const nir_search_variable search104_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search104_0.value, &search104_1.value, &search104_2.value },
   NULL,
};
   
static const nir_search_variable replace104_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace104_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace104_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace104_0.value, &replace104_1.value, &replace104_2.value },
   NULL,
};
   
static const nir_search_variable search105_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search105_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_variable search105_2 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_bool32,
   NULL,
};
static const nir_search_expression search105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search105_0.value, &search105_1.value, &search105_2.value },
   NULL,
};
   
static const nir_search_variable replace105_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace105_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace105_0.value, &replace105_1.value },
   NULL,
};
   
static const nir_search_variable search290_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search290_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_constant search290_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search290 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search290_0.value, &search290_1.value, &search290_2.value },
   NULL,
};
   
static const nir_search_variable replace290 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search291_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search291_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

static const nir_search_constant search291_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search291 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search291_0.value, &search291_1.value, &search291_2.value },
   NULL,
};
   
static const nir_search_variable replace291_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace291 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace291_0.value },
   NULL,
};
   
static const nir_search_variable search292_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search292_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant search292_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search292 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search292_0.value, &search292_1.value, &search292_2.value },
   NULL,
};
   
static const nir_search_variable replace292_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace292 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace292_0.value },
   NULL,
};
   
static const nir_search_variable search293_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search293_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant search293_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search293 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search293_0.value, &search293_1.value, &search293_2.value },
   NULL,
};
   
static const nir_search_variable replace293_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace293_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace293_0_0.value },
   NULL,
};
static const nir_search_expression replace293 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace293_0.value },
   NULL,
};
   
static const nir_search_variable search294_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search294_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_constant search294_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};
static const nir_search_expression search294 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search294_0.value, &search294_1.value, &search294_2.value },
   NULL,
};
   
static const nir_search_variable replace294_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace294_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace294_0_0.value },
   NULL,
};
static const nir_search_expression replace294 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace294_0.value },
   NULL,
};
   
static const nir_search_variable search295_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search295_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};

static const nir_search_constant search295_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
static const nir_search_expression search295 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search295_0.value, &search295_1.value, &search295_2.value },
   NULL,
};
   
static const nir_search_variable replace295_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace295_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace295_0_0_0.value },
   NULL,
};
static const nir_search_expression replace295_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace295_0_0.value },
   NULL,
};
static const nir_search_expression replace295 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace295_0.value },
   NULL,
};
   
static const nir_search_constant search296_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_variable search296_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search296_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search296 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search296_0.value, &search296_1.value, &search296_2.value },
   NULL,
};
   
static const nir_search_variable replace296 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_constant search297_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

static const nir_search_variable search297_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search297_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search297 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search297_0.value, &search297_1.value, &search297_2.value },
   NULL,
};
   
static const nir_search_variable replace297 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search298_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search298_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search298_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search298_1_0.value },
   (is_used_once),
};

static const nir_search_variable search298_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search298_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search298_2_0.value },
   NULL,
};
static const nir_search_expression search298 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search298_0.value, &search298_1.value, &search298_2.value },
   NULL,
};
   
static const nir_search_variable replace298_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace298_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace298_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace298_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace298_0_0.value, &replace298_0_1.value, &replace298_0_2.value },
   NULL,
};
static const nir_search_expression replace298 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace298_0.value },
   NULL,
};
   
static const nir_search_variable search299_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search299_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search299_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search299 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search299_0.value, &search299_1.value, &search299_2.value },
   NULL,
};
   
static const nir_search_variable replace299_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace299_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace299_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace299_0_0.value, &replace299_0_1.value },
   NULL,
};

static const nir_search_variable replace299_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace299_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace299 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace299_0.value, &replace299_1.value, &replace299_2.value },
   NULL,
};
   
static const nir_search_variable search300_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search300_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search300_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search300 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search300_0.value, &search300_1.value, &search300_2.value },
   NULL,
};
   
static const nir_search_variable replace300 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search343_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search343_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &search343_0_0_0.value },
   NULL,
};

static const nir_search_constant search343_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search343_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search343_0_0.value, &search343_0_1.value },
   NULL,
};

static const nir_search_variable search343_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search343_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &search343_1_0.value },
   NULL,
};

static const nir_search_constant search343_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search343 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search343_0.value, &search343_1.value, &search343_2.value },
   NULL,
};
   
static const nir_search_variable replace343_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace343 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace343_0.value },
   NULL,
};
   
static const nir_search_variable search344_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search344_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &search344_0_0_0.value },
   NULL,
};

static const nir_search_constant search344_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search344_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search344_0_0.value, &search344_0_1.value },
   NULL,
};

static const nir_search_variable search344_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search344_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &search344_1_0.value },
   NULL,
};

static const nir_search_constant search344_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search344 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search344_0.value, &search344_1.value, &search344_2.value },
   NULL,
};
   
static const nir_search_variable replace344_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace344 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &replace344_0.value },
   NULL,
};
   
static const nir_search_variable search345_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search345_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &search345_0_0_0.value },
   NULL,
};

static const nir_search_constant search345_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search345_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search345_0_0.value, &search345_0_1.value },
   NULL,
};

static const nir_search_variable search345_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search345_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &search345_1_0.value },
   NULL,
};

static const nir_search_constant search345_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search345 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search345_0.value, &search345_1.value, &search345_2.value },
   NULL,
};
   
static const nir_search_variable replace345_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace345 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &replace345_0.value },
   NULL,
};
   
static const nir_search_variable search346_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search346_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search346_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search346_0_0.value, &search346_0_1.value },
   NULL,
};

static const nir_search_variable search346_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search346_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &search346_1_0.value },
   NULL,
};

static const nir_search_constant search346_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search346 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search346_0.value, &search346_1.value, &search346_2.value },
   NULL,
};
   
static const nir_search_variable replace346_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace346 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &replace346_0.value },
   NULL,
};
   
static const nir_search_variable search347_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search347_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search347_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search347_0_0.value, &search347_0_1.value },
   NULL,
};

static const nir_search_variable search347_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search347_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &search347_1_0.value },
   NULL,
};

static const nir_search_constant search347_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search347 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search347_0.value, &search347_1.value, &search347_2.value },
   NULL,
};
   
static const nir_search_variable replace347_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace347 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &replace347_0.value },
   NULL,
};
   
static const nir_search_variable search348_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search348_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search348_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search348_0_0.value, &search348_0_1.value },
   NULL,
};

static const nir_search_variable search348_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search348_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &search348_1_0.value },
   NULL,
};

static const nir_search_constant search348_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search348 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search348_0.value, &search348_1.value, &search348_2.value },
   NULL,
};
   
static const nir_search_variable replace348_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace348 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &replace348_0.value },
   NULL,
};
   
static const nir_search_variable search349_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search349_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search349_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search349_0_0.value, &search349_0_1.value },
   NULL,
};

static const nir_search_variable search349_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search349_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &search349_1_0.value },
   NULL,
};

static const nir_search_constant search349_2 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search349 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search349_0.value, &search349_1.value, &search349_2.value },
   NULL,
};
   
static const nir_search_variable replace349_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace349 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &replace349_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_bcsel_xforms[] = {
   { &search99, &replace99.value, 0 },
   { &search100, &replace100.value, 0 },
   { &search101, &replace101.value, 0 },
   { &search102, &replace102.value, 0 },
   { &search103, &replace103.value, 0 },
   { &search104, &replace104.value, 0 },
   { &search105, &replace105.value, 0 },
   { &search290, &replace290.value, 0 },
   { &search291, &replace291.value, 0 },
   { &search292, &replace292.value, 0 },
   { &search293, &replace293.value, 0 },
   { &search294, &replace294.value, 0 },
   { &search295, &replace295.value, 0 },
   { &search296, &replace296.value, 0 },
   { &search297, &replace297.value, 0 },
   { &search298, &replace298.value, 0 },
   { &search299, &replace299.value, 0 },
   { &search300, &replace300.value, 0 },
   { &search343, &replace343.value, 0 },
   { &search344, &replace344.value, 0 },
   { &search345, &replace345.value, 0 },
   { &search346, &replace346.value, 0 },
   { &search347, &replace347.value, 0 },
   { &search348, &replace348.value, 0 },
   { &search349, &replace349.value, 0 },
};
   
static const nir_search_variable search108_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search108_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search108 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search108_0.value, &search108_1.value },
   NULL,
};
   
static const nir_search_variable replace108 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search115_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search115_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search115_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search115_1_0.value },
   NULL,
};
static const nir_search_expression search115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search115_0.value, &search115_1.value },
   NULL,
};
   
static const nir_search_variable replace115_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace115_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace115_0_0.value },
   NULL,
};
static const nir_search_expression replace115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace115_0.value },
   NULL,
};
   
static const nir_search_variable search117_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search117_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search117_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search117_1_0_0.value },
   NULL,
};
static const nir_search_expression search117_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search117_1_0.value },
   NULL,
};
static const nir_search_expression search117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search117_0.value, &search117_1.value },
   NULL,
};
   
static const nir_search_variable replace117_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace117_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace117_0_0.value },
   NULL,
};
static const nir_search_expression replace117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace117_0.value },
   NULL,
};
   
static const nir_search_variable search119_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search119_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search119_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search119_1_0.value },
   NULL,
};
static const nir_search_expression search119 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search119_0.value, &search119_1.value },
   NULL,
};
   
static const nir_search_variable replace119 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search131_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search131_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search131_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search131_0_0_0_0.value, &search131_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search131_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search131_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search131_0_0_0.value, &search131_0_0_1.value },
   NULL,
};

static const nir_search_variable search131_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search131_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search131_0_0.value, &search131_0_1.value },
   NULL,
};

static const nir_search_variable search131_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search131_0.value, &search131_1.value },
   NULL,
};
   
static const nir_search_variable replace131_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace131_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace131_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace131_0_0.value, &replace131_0_1.value },
   NULL,
};

static const nir_search_variable replace131_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace131_0.value, &replace131_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imin_xforms[] = {
   { &search108, &replace108.value, 0 },
   { &search115, &replace115.value, 0 },
   { &search117, &replace117.value, 0 },
   { &search119, &replace119.value, 0 },
   { &search131, &replace131.value, 0 },
};
   
static const nir_search_variable search109_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search109_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search109 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search109_0.value, &search109_1.value },
   NULL,
};
   
static const nir_search_variable replace109 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search113_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search113_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search113_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search113_1_0.value },
   NULL,
};
static const nir_search_expression search113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search113_0.value, &search113_1.value },
   NULL,
};
   
static const nir_search_variable replace113_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace113_0.value },
   NULL,
};
   
static const nir_search_variable search121_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search121_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search121_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search121_1_0_0.value },
   NULL,
};
static const nir_search_expression search121_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search121_1_0.value },
   NULL,
};
static const nir_search_expression search121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search121_0.value, &search121_1.value },
   NULL,
};
   
static const nir_search_variable replace121 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search123_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search123_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search123_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search123_1_0.value },
   NULL,
};
static const nir_search_expression search123 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search123_0.value, &search123_1.value },
   NULL,
};
   
static const nir_search_variable replace123_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace123 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace123_0.value },
   NULL,
};
   
static const nir_search_variable search125_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search125_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search125_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search125_1_0.value },
   NULL,
};
static const nir_search_expression search125 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search125_0.value, &search125_1.value },
   NULL,
};
   
static const nir_search_variable replace125_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace125 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace125_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imax_xforms[] = {
   { &search109, &replace109.value, 0 },
   { &search113, &replace113.value, 0 },
   { &search121, &replace121.value, 0 },
   { &search123, &replace123.value, 0 },
   { &search125, &replace125.value, 0 },
};
   
static const nir_search_variable search110_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search110_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search110_0.value, &search110_1.value },
   NULL,
};
   
static const nir_search_variable replace110 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search132_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search132_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search132_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search132_0_0_0_0.value, &search132_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search132_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search132_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search132_0_0_0.value, &search132_0_0_1.value },
   NULL,
};

static const nir_search_variable search132_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search132_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search132_0_0.value, &search132_0_1.value },
   NULL,
};

static const nir_search_variable search132_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search132_0.value, &search132_1.value },
   NULL,
};
   
static const nir_search_variable replace132_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace132_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace132_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace132_0_0.value, &replace132_0_1.value },
   NULL,
};

static const nir_search_variable replace132_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace132_0.value, &replace132_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_umin_xforms[] = {
   { &search110, &replace110.value, 0 },
   { &search132, &replace132.value, 0 },
};
   
static const nir_search_variable search111_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search111_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search111_0.value, &search111_1.value },
   NULL,
};
   
static const nir_search_variable replace111 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_umax_xforms[] = {
   { &search111, &replace111.value, 0 },
};
   
static const nir_search_variable search128_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search128 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search128_0.value },
   NULL,
};
   
static const nir_search_variable replace128_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace128_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace128_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace128_0_0.value, &replace128_0_1.value },
   NULL,
};

static const nir_search_constant replace128_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace128 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace128_0.value, &replace128_1.value },
   NULL,
};
   
static const nir_search_variable search129_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search129_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search129_0_0.value },
   NULL,
};
static const nir_search_expression search129 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search129_0.value },
   NULL,
};
   
static const nir_search_variable replace129_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace129 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace129_0.value },
   NULL,
};
   
static const nir_search_variable search223_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search223_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search223_0_0_0.value },
   NULL,
};

static const nir_search_variable search223_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search223_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search223_0_1_0.value },
   NULL,
};
static const nir_search_expression search223_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search223_0_0.value, &search223_0_1.value },
   NULL,
};
static const nir_search_expression search223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search223_0.value },
   NULL,
};
   
static const nir_search_variable replace223_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace223_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace223_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace223_0_0.value, &replace223_0_1.value },
   NULL,
};
static const nir_search_expression replace223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace223_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsat_xforms[] = {
   { &search128, &replace128.value, 10 },
   { &search129, &replace129.value, 0 },
   { &search223, &replace223.value, 0 },
};
   
static const nir_search_variable search135_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search135_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search135_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search135_0_0_0.value, &search135_0_0_1.value },
   NULL,
};

static const nir_search_constant search135_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression search135_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search135_0_0.value, &search135_0_1.value },
   NULL,
};

static const nir_search_constant search135_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search135_0.value, &search135_1.value },
   NULL,
};
   
static const nir_search_variable replace135_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace135_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace135_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace135_0_0.value, &replace135_0_1.value },
   NULL,
};

static const nir_search_constant replace135_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression replace135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace135_0.value, &replace135_1.value },
   NULL,
};
   
static const nir_search_variable search365_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search365_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search365 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search365_0.value, &search365_1.value },
   NULL,
};
   
static const nir_search_variable replace365_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace365_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace365_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression replace365_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace365_0_1_0.value, &replace365_0_1_1.value },
   NULL,
};
static const nir_search_expression replace365_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace365_0_0.value, &replace365_0_1.value },
   NULL,
};

static const nir_search_constant replace365_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression replace365 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace365_0.value, &replace365_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_u8_xforms[] = {
   { &search135, &replace135.value, 0 },
   { &search365, &replace365.value, 33 },
};
   
static const nir_search_variable search136_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search136_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search136_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search136_0_0.value, &search136_0_1.value },
   (is_used_once),
};

static const nir_search_variable search136_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search136_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search136_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search136_1_0.value, &search136_1_1.value },
   NULL,
};
static const nir_search_expression search136 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search136_0.value, &search136_1.value },
   NULL,
};
   
static const nir_search_variable replace136_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace136_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace136_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace136_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace136_1_0.value, &replace136_1_1.value },
   NULL,
};
static const nir_search_expression replace136 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace136_0.value, &replace136_1.value },
   NULL,
};
   
static const nir_search_variable search137_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search137_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search137_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search137_0_0.value, &search137_0_1.value },
   (is_used_once),
};

static const nir_search_variable search137_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search137_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search137_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search137_1_0.value, &search137_1_1.value },
   NULL,
};
static const nir_search_expression search137 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search137_0.value, &search137_1.value },
   NULL,
};
   
static const nir_search_variable replace137_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace137_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace137_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace137_0_0.value, &replace137_0_1.value },
   NULL,
};

static const nir_search_variable replace137_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace137 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace137_0.value, &replace137_1.value },
   NULL,
};
   
static const nir_search_variable search138_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search138_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search138_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search138_0_0.value, &search138_0_1.value },
   (is_used_once),
};

static const nir_search_variable search138_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search138_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search138_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search138_1_0.value, &search138_1_1.value },
   NULL,
};
static const nir_search_expression search138 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search138_0.value, &search138_1.value },
   NULL,
};
   
static const nir_search_variable replace138_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace138_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace138_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace138_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace138_1_0.value, &replace138_1_1.value },
   NULL,
};
static const nir_search_expression replace138 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace138_0.value, &replace138_1.value },
   NULL,
};
   
static const nir_search_variable search139_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search139_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search139_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search139_0_0.value, &search139_0_1.value },
   (is_used_once),
};

static const nir_search_variable search139_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search139_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search139_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search139_1_0.value, &search139_1_1.value },
   NULL,
};
static const nir_search_expression search139 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search139_0.value, &search139_1.value },
   NULL,
};
   
static const nir_search_variable replace139_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace139_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace139_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace139_0_0.value, &replace139_0_1.value },
   NULL,
};

static const nir_search_variable replace139_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace139 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace139_0.value, &replace139_1.value },
   NULL,
};
   
static const nir_search_variable search140_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search140_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search140_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search140_0_0.value, &search140_0_1.value },
   NULL,
};

static const nir_search_variable search140_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search140_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search140_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search140_1_0.value, &search140_1_1.value },
   NULL,
};
static const nir_search_expression search140 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search140_0.value, &search140_1.value },
   NULL,
};
   
static const nir_search_variable replace140_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace140_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace140_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace140_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace140_1_0.value, &replace140_1_1.value },
   NULL,
};
static const nir_search_expression replace140 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace140_0.value, &replace140_1.value },
   NULL,
};
   
static const nir_search_variable search141_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search141_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search141_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search141_0_0.value, &search141_0_1.value },
   NULL,
};

static const nir_search_variable search141_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search141_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search141_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search141_1_0.value, &search141_1_1.value },
   NULL,
};
static const nir_search_expression search141 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search141_0.value, &search141_1.value },
   NULL,
};
   
static const nir_search_variable replace141_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace141_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace141_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace141_0_0.value, &replace141_0_1.value },
   NULL,
};

static const nir_search_variable replace141_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace141 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace141_0.value, &replace141_1.value },
   NULL,
};
   
static const nir_search_variable search142_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search142_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search142_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search142_0_0.value, &search142_0_1.value },
   NULL,
};

static const nir_search_variable search142_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search142_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search142_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search142_1_0.value, &search142_1_1.value },
   NULL,
};
static const nir_search_expression search142 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search142_0.value, &search142_1.value },
   NULL,
};
   
static const nir_search_variable replace142_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace142_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace142_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace142_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace142_1_0.value, &replace142_1_1.value },
   NULL,
};
static const nir_search_expression replace142 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace142_0.value, &replace142_1.value },
   NULL,
};
   
static const nir_search_variable search143_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search143_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search143_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search143_0_0.value, &search143_0_1.value },
   NULL,
};

static const nir_search_variable search143_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search143_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search143_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search143_1_0.value, &search143_1_1.value },
   NULL,
};
static const nir_search_expression search143 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search143_0.value, &search143_1.value },
   NULL,
};
   
static const nir_search_variable replace143_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace143_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace143_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace143_0_0.value, &replace143_0_1.value },
   NULL,
};

static const nir_search_variable replace143_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace143 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace143_0.value, &replace143_1.value },
   NULL,
};
   
static const nir_search_variable search152_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search152_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search152_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search152_0_0.value, &search152_0_1.value },
   (is_used_once),
};

static const nir_search_variable search152_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search152_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search152_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search152_1_0.value, &search152_1_1.value },
   NULL,
};
static const nir_search_expression search152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search152_0.value, &search152_1.value },
   NULL,
};
   
static const nir_search_variable replace152_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace152_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace152_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace152_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace152_1_0.value, &replace152_1_1.value },
   NULL,
};
static const nir_search_expression replace152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace152_0.value, &replace152_1.value },
   NULL,
};
   
static const nir_search_variable search153_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search153_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search153_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search153_0_0.value, &search153_0_1.value },
   (is_used_once),
};

static const nir_search_variable search153_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search153_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search153_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search153_1_0.value, &search153_1_1.value },
   NULL,
};
static const nir_search_expression search153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search153_0.value, &search153_1.value },
   NULL,
};
   
static const nir_search_variable replace153_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace153_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace153_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace153_0_0.value, &replace153_0_1.value },
   NULL,
};

static const nir_search_variable replace153_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace153_0.value, &replace153_1.value },
   NULL,
};
   
static const nir_search_variable search154_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search154_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search154_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search154_0_0.value, &search154_0_1.value },
   (is_used_once),
};

static const nir_search_variable search154_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search154_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search154_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search154_1_0.value, &search154_1_1.value },
   NULL,
};
static const nir_search_expression search154 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search154_0.value, &search154_1.value },
   NULL,
};
   
static const nir_search_variable replace154_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace154_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace154_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace154_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace154_1_0.value, &replace154_1_1.value },
   NULL,
};
static const nir_search_expression replace154 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace154_0.value, &replace154_1.value },
   NULL,
};
   
static const nir_search_variable search155_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search155_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search155_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search155_0_0.value, &search155_0_1.value },
   (is_used_once),
};

static const nir_search_variable search155_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search155_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search155_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search155_1_0.value, &search155_1_1.value },
   NULL,
};
static const nir_search_expression search155 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search155_0.value, &search155_1.value },
   NULL,
};
   
static const nir_search_variable replace155_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace155_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace155_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace155_0_0.value, &replace155_0_1.value },
   NULL,
};

static const nir_search_variable replace155_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace155 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace155_0.value, &replace155_1.value },
   NULL,
};
   
static const nir_search_variable search156_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search156_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search156_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search156_0_0.value, &search156_0_1.value },
   (is_used_once),
};

static const nir_search_variable search156_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search156_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search156_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search156_1_0.value, &search156_1_1.value },
   NULL,
};
static const nir_search_expression search156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search156_0.value, &search156_1.value },
   NULL,
};
   
static const nir_search_variable replace156_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace156_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace156_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace156_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace156_1_0.value, &replace156_1_1.value },
   NULL,
};
static const nir_search_expression replace156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace156_0.value, &replace156_1.value },
   NULL,
};
   
static const nir_search_variable search157_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search157_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search157_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search157_0_0.value, &search157_0_1.value },
   (is_used_once),
};

static const nir_search_variable search157_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search157_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search157_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search157_1_0.value, &search157_1_1.value },
   NULL,
};
static const nir_search_expression search157 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search157_0.value, &search157_1.value },
   NULL,
};
   
static const nir_search_variable replace157_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace157_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace157_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace157_0_0.value, &replace157_0_1.value },
   NULL,
};

static const nir_search_variable replace157_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace157 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace157_0.value, &replace157_1.value },
   NULL,
};
   
static const nir_search_variable search158_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search158_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search158_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search158_0_0.value, &search158_0_1.value },
   (is_used_once),
};

static const nir_search_variable search158_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search158_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search158_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search158_1_0.value, &search158_1_1.value },
   NULL,
};
static const nir_search_expression search158 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search158_0.value, &search158_1.value },
   NULL,
};
   
static const nir_search_variable replace158_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace158_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace158_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace158_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace158_1_0.value, &replace158_1_1.value },
   NULL,
};
static const nir_search_expression replace158 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace158_0.value, &replace158_1.value },
   NULL,
};
   
static const nir_search_variable search159_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search159_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search159_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search159_0_0.value, &search159_0_1.value },
   (is_used_once),
};

static const nir_search_variable search159_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search159_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search159_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search159_1_0.value, &search159_1_1.value },
   NULL,
};
static const nir_search_expression search159 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search159_0.value, &search159_1.value },
   NULL,
};
   
static const nir_search_variable replace159_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace159_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace159_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace159_0_0.value, &replace159_0_1.value },
   NULL,
};

static const nir_search_variable replace159_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace159 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace159_0.value, &replace159_1.value },
   NULL,
};
   
static const nir_search_variable search168_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_variable search168_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search168_1_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search168_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search168_1_0.value, &search168_1_1.value },
   NULL,
};
static const nir_search_expression search168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search168_0.value, &search168_1.value },
   NULL,
};
   
static const nir_search_constant replace168 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search169_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_variable search169_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search169_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search169_1_0.value },
   NULL,
};
static const nir_search_expression search169 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search169_0.value, &search169_1.value },
   NULL,
};
   
static const nir_search_constant replace169 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search238_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search238_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search238_0.value, &search238_1.value },
   NULL,
};
   
static const nir_search_variable replace238 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search239_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search239_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search239_0.value, &search239_1.value },
   NULL,
};
   
static const nir_search_variable replace239 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search240_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search240_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search240_0.value, &search240_1.value },
   NULL,
};
   
static const nir_search_constant replace240 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search245_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search245_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search245_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search245_0_0.value, &search245_0_1.value },
   NULL,
};

static const nir_search_variable search245_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search245 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search245_0.value, &search245_1.value },
   NULL,
};
   
static const nir_search_variable replace245 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search246_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search246_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search246_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search246_0_0.value, &search246_0_1.value },
   NULL,
};

static const nir_search_variable search246_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search246_0.value, &search246_1.value },
   NULL,
};
   
static const nir_search_variable replace246_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace246_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace246_0.value, &replace246_1.value },
   NULL,
};
   
static const nir_search_variable search250_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search250_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search250_0_0.value },
   NULL,
};

static const nir_search_variable search250_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search250_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search250_1_0.value },
   NULL,
};
static const nir_search_expression search250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0.value, &search250_1.value },
   NULL,
};
   
static const nir_search_variable replace250_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace250_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace250_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace250_0_0.value, &replace250_0_1.value },
   NULL,
};
static const nir_search_expression replace250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace250_0.value },
   NULL,
};
   
static const nir_search_variable search398_0_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search398_0_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search398_0_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_0_0_0_1_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0_0_0_0_1_0_0.value, &search398_0_0_0_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_0_0_0_1_0.value, &search398_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search398_0_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_1_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0_1_0_0_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_1_0_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search398_0_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_1_0_0_1_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0_1_0_0_1_0_0.value, &search398_0_0_0_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_1_0_0_1_0.value, &search398_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_1_0_0_0.value, &search398_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0_1_0_0.value, &search398_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_0_0_0_1_0.value, &search398_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0_0_0_0.value, &search398_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
static const nir_search_expression search398_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_0_0_0.value, &search398_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search398_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_0_0.value, &search398_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search398_0_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_0_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search398_0_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_0_0_0_1_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0_0_0_0_1_0_0.value, &search398_0_0_0_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_0_0_0_1_0.value, &search398_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0_0_0_0.value, &search398_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_0_0.value, &search398_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search398_0_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_1_0_0_0_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0_1_0_0_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_1_0_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_0_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search398_0_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_1_0_0_1_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0_1_0_0_1_0_0.value, &search398_0_0_0_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_1_0_0_1_0.value, &search398_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_1_0_0_0.value, &search398_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0_1_0_0.value, &search398_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0_0_1_0.value, &search398_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_1_0_0_0.value, &search398_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
static const nir_search_expression search398_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0_1_0_0.value, &search398_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search398_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_0_0_0_1_0.value, &search398_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0_0_0_0.value, &search398_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x55555555 /* 1431655765 */ },
};
static const nir_search_expression search398_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_0_0_0.value, &search398_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search398_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_0_0.value, &search398_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search398_1_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search398_1_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_0_0_0_1_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0_0_0_0_1_0_0.value, &search398_1_0_0_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_0_0_0_1_0.value, &search398_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search398_1_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_1_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0_1_0_0_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_1_0_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search398_1_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_1_0_0_1_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0_1_0_0_1_0_0.value, &search398_1_0_0_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_1_0_0_1_0.value, &search398_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_1_0_0_0.value, &search398_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0_1_0_0.value, &search398_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_0_0_0_1_0.value, &search398_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0_0_0_0.value, &search398_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
static const nir_search_expression search398_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_0_0_0.value, &search398_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search398_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_0_0.value, &search398_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search398_1_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_0_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search398_1_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_0_0_0_1_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0_0_0_0_1_0_0.value, &search398_1_0_0_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_0_0_0_1_0.value, &search398_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0_0_0_0.value, &search398_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_0_0.value, &search398_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search398_1_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_1_0_0_0_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0_1_0_0_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_1_0_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search398_1_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search398_1_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search398_1_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_1_0_0_1_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0_1_0_0_1_0_0.value, &search398_1_0_0_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_1_0_0_1_0.value, &search398_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_1_0_0_0.value, &search398_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0_1_0_0.value, &search398_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search398_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0_0_1_0.value, &search398_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_1_0_0_0.value, &search398_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
static const nir_search_expression search398_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0_1_0_0.value, &search398_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search398_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0_0_1_0.value, &search398_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search398_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_1_0_0_0.value, &search398_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xaaaaaaaa /* 2863311530 */ },
};
static const nir_search_expression search398_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search398_1_0_0.value, &search398_1_0_1.value },
   NULL,
};

static const nir_search_constant search398_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search398_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search398_1_0.value, &search398_1_1.value },
   NULL,
};
static const nir_search_expression search398 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search398_0.value, &search398_1.value },
   NULL,
};
   
static const nir_search_variable replace398_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace398 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_reverse,
   { &replace398_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search136, &replace136.value, 0 },
   { &search137, &replace137.value, 0 },
   { &search138, &replace138.value, 0 },
   { &search139, &replace139.value, 0 },
   { &search140, &replace140.value, 0 },
   { &search141, &replace141.value, 0 },
   { &search142, &replace142.value, 0 },
   { &search143, &replace143.value, 0 },
   { &search152, &replace152.value, 0 },
   { &search153, &replace153.value, 0 },
   { &search154, &replace154.value, 0 },
   { &search155, &replace155.value, 0 },
   { &search156, &replace156.value, 0 },
   { &search157, &replace157.value, 0 },
   { &search158, &replace158.value, 0 },
   { &search159, &replace159.value, 0 },
   { &search168, &replace168.value, 0 },
   { &search169, &replace169.value, 0 },
   { &search238, &replace238.value, 0 },
   { &search239, &replace239.value, 0 },
   { &search240, &replace240.value, 0 },
   { &search245, &replace245.value, 0 },
   { &search246, &replace246.value, 0 },
   { &search250, &replace250.value, 0 },
   { &search398, &replace398.value, 0 },
};
   
static const nir_search_variable search144_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search144_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search144_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search144_0_0.value, &search144_0_1.value },
   (is_used_once),
};

static const nir_search_variable search144_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search144_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search144_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search144_1_0.value, &search144_1_1.value },
   NULL,
};
static const nir_search_expression search144 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search144_0.value, &search144_1.value },
   NULL,
};
   
static const nir_search_variable replace144_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace144_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace144_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace144_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace144_1_0.value, &replace144_1_1.value },
   NULL,
};
static const nir_search_expression replace144 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace144_0.value, &replace144_1.value },
   NULL,
};
   
static const nir_search_variable search145_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search145_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search145_0_0.value, &search145_0_1.value },
   (is_used_once),
};

static const nir_search_variable search145_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search145_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search145_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search145_1_0.value, &search145_1_1.value },
   NULL,
};
static const nir_search_expression search145 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search145_0.value, &search145_1.value },
   NULL,
};
   
static const nir_search_variable replace145_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace145_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace145_0_0.value, &replace145_0_1.value },
   NULL,
};

static const nir_search_variable replace145_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace145 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace145_0.value, &replace145_1.value },
   NULL,
};
   
static const nir_search_variable search146_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search146_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search146_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search146_0_0.value, &search146_0_1.value },
   (is_used_once),
};

static const nir_search_variable search146_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search146_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search146_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search146_1_0.value, &search146_1_1.value },
   NULL,
};
static const nir_search_expression search146 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search146_0.value, &search146_1.value },
   NULL,
};
   
static const nir_search_variable replace146_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace146_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace146_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace146_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace146_1_0.value, &replace146_1_1.value },
   NULL,
};
static const nir_search_expression replace146 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace146_0.value, &replace146_1.value },
   NULL,
};
   
static const nir_search_variable search147_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search147_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search147_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search147_0_0.value, &search147_0_1.value },
   (is_used_once),
};

static const nir_search_variable search147_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search147_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search147_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search147_1_0.value, &search147_1_1.value },
   NULL,
};
static const nir_search_expression search147 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search147_0.value, &search147_1.value },
   NULL,
};
   
static const nir_search_variable replace147_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace147_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace147_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace147_0_0.value, &replace147_0_1.value },
   NULL,
};

static const nir_search_variable replace147_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace147 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace147_0.value, &replace147_1.value },
   NULL,
};
   
static const nir_search_variable search148_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search148_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search148_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search148_0_0.value, &search148_0_1.value },
   NULL,
};

static const nir_search_variable search148_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search148_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search148_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search148_1_0.value, &search148_1_1.value },
   NULL,
};
static const nir_search_expression search148 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search148_0.value, &search148_1.value },
   NULL,
};
   
static const nir_search_variable replace148_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace148_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace148_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace148_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace148_1_0.value, &replace148_1_1.value },
   NULL,
};
static const nir_search_expression replace148 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace148_0.value, &replace148_1.value },
   NULL,
};
   
static const nir_search_variable search149_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search149_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search149_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search149_0_0.value, &search149_0_1.value },
   NULL,
};

static const nir_search_variable search149_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search149_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search149_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search149_1_0.value, &search149_1_1.value },
   NULL,
};
static const nir_search_expression search149 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search149_0.value, &search149_1.value },
   NULL,
};
   
static const nir_search_variable replace149_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace149_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace149_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace149_0_0.value, &replace149_0_1.value },
   NULL,
};

static const nir_search_variable replace149_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace149 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace149_0.value, &replace149_1.value },
   NULL,
};
   
static const nir_search_variable search150_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search150_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search150_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search150_0_0.value, &search150_0_1.value },
   NULL,
};

static const nir_search_variable search150_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search150_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search150_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search150_1_0.value, &search150_1_1.value },
   NULL,
};
static const nir_search_expression search150 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search150_0.value, &search150_1.value },
   NULL,
};
   
static const nir_search_variable replace150_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace150_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace150_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace150_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace150_1_0.value, &replace150_1_1.value },
   NULL,
};
static const nir_search_expression replace150 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace150_0.value, &replace150_1.value },
   NULL,
};
   
static const nir_search_variable search151_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search151_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search151_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search151_0_0.value, &search151_0_1.value },
   NULL,
};

static const nir_search_variable search151_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search151_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search151_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search151_1_0.value, &search151_1_1.value },
   NULL,
};
static const nir_search_expression search151 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_iand,
   { &search151_0.value, &search151_1.value },
   NULL,
};
   
static const nir_search_variable replace151_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace151_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace151_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace151_0_0.value, &replace151_0_1.value },
   NULL,
};

static const nir_search_variable replace151_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace151 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace151_0.value, &replace151_1.value },
   NULL,
};
   
static const nir_search_variable search160_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search160_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search160_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search160_0_0.value, &search160_0_1.value },
   (is_used_once),
};

static const nir_search_variable search160_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search160_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search160_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search160_1_0.value, &search160_1_1.value },
   NULL,
};
static const nir_search_expression search160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search160_0.value, &search160_1.value },
   NULL,
};
   
static const nir_search_variable replace160_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace160_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace160_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace160_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace160_1_0.value, &replace160_1_1.value },
   NULL,
};
static const nir_search_expression replace160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace160_0.value, &replace160_1.value },
   NULL,
};
   
static const nir_search_variable search161_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search161_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search161_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search161_0_0.value, &search161_0_1.value },
   (is_used_once),
};

static const nir_search_variable search161_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search161_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search161_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search161_1_0.value, &search161_1_1.value },
   NULL,
};
static const nir_search_expression search161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search161_0.value, &search161_1.value },
   NULL,
};
   
static const nir_search_variable replace161_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace161_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace161_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace161_0_0.value, &replace161_0_1.value },
   NULL,
};

static const nir_search_variable replace161_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace161_0.value, &replace161_1.value },
   NULL,
};
   
static const nir_search_variable search162_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search162_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search162_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search162_0_0.value, &search162_0_1.value },
   (is_used_once),
};

static const nir_search_variable search162_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search162_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search162_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search162_1_0.value, &search162_1_1.value },
   NULL,
};
static const nir_search_expression search162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search162_0.value, &search162_1.value },
   NULL,
};
   
static const nir_search_variable replace162_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace162_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace162_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace162_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace162_1_0.value, &replace162_1_1.value },
   NULL,
};
static const nir_search_expression replace162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace162_0.value, &replace162_1.value },
   NULL,
};
   
static const nir_search_variable search163_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search163_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search163_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search163_0_0.value, &search163_0_1.value },
   (is_used_once),
};

static const nir_search_variable search163_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search163_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search163_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search163_1_0.value, &search163_1_1.value },
   NULL,
};
static const nir_search_expression search163 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search163_0.value, &search163_1.value },
   NULL,
};
   
static const nir_search_variable replace163_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace163_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace163_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace163_0_0.value, &replace163_0_1.value },
   NULL,
};

static const nir_search_variable replace163_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace163 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace163_0.value, &replace163_1.value },
   NULL,
};
   
static const nir_search_variable search164_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search164_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search164_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search164_0_0.value, &search164_0_1.value },
   (is_used_once),
};

static const nir_search_variable search164_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search164_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search164_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search164_1_0.value, &search164_1_1.value },
   NULL,
};
static const nir_search_expression search164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search164_0.value, &search164_1.value },
   NULL,
};
   
static const nir_search_variable replace164_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace164_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace164_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace164_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace164_1_0.value, &replace164_1_1.value },
   NULL,
};
static const nir_search_expression replace164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace164_0.value, &replace164_1.value },
   NULL,
};
   
static const nir_search_variable search165_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search165_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search165_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search165_0_0.value, &search165_0_1.value },
   (is_used_once),
};

static const nir_search_variable search165_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search165_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search165_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search165_1_0.value, &search165_1_1.value },
   NULL,
};
static const nir_search_expression search165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search165_0.value, &search165_1.value },
   NULL,
};
   
static const nir_search_variable replace165_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace165_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace165_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace165_0_0.value, &replace165_0_1.value },
   NULL,
};

static const nir_search_variable replace165_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace165_0.value, &replace165_1.value },
   NULL,
};
   
static const nir_search_variable search166_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search166_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search166_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search166_0_0.value, &search166_0_1.value },
   (is_used_once),
};

static const nir_search_variable search166_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search166_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search166_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search166_1_0.value, &search166_1_1.value },
   NULL,
};
static const nir_search_expression search166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search166_0.value, &search166_1.value },
   NULL,
};
   
static const nir_search_variable replace166_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace166_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace166_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace166_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace166_1_0.value, &replace166_1_1.value },
   NULL,
};
static const nir_search_expression replace166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace166_0.value, &replace166_1.value },
   NULL,
};
   
static const nir_search_variable search167_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search167_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search167_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search167_0_0.value, &search167_0_1.value },
   (is_used_once),
};

static const nir_search_variable search167_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search167_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search167_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search167_1_0.value, &search167_1_1.value },
   NULL,
};
static const nir_search_expression search167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search167_0.value, &search167_1.value },
   NULL,
};
   
static const nir_search_variable replace167_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace167_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace167_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace167_0_0.value, &replace167_0_1.value },
   NULL,
};

static const nir_search_variable replace167_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace167_0.value, &replace167_1.value },
   NULL,
};
   
static const nir_search_variable search170_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search170_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search170_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search170_0_0.value, &search170_0_1.value },
   NULL,
};

static const nir_search_variable search170_1_0 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search170_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search170_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search170_1_0.value, &search170_1_1.value },
   NULL,
};
static const nir_search_expression search170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search170_0.value, &search170_1.value },
   NULL,
};
   
static const nir_search_variable replace170_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace170_0_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace170_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace170_0_0.value, &replace170_0_1.value },
   NULL,
};

static const nir_search_constant replace170_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace170_0.value, &replace170_1.value },
   NULL,
};
   
static const nir_search_variable search224_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search224_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search224_0.value, &search224_1.value },
   NULL,
};
   
static const nir_search_variable replace224_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace224_0.value },
   NULL,
};
   
static const nir_search_variable search235_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search235_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search235_0.value, &search235_1.value },
   NULL,
};
   
static const nir_search_variable replace235 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search236_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search236_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search236_0.value, &search236_1.value },
   NULL,
};
   
static const nir_search_variable replace236 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search237_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search237_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search237_0.value, &search237_1.value },
   NULL,
};
   
static const nir_search_constant replace237 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search247_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search247_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search247_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search247_0_0.value, &search247_0_1.value },
   NULL,
};

static const nir_search_variable search247_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search247_0.value, &search247_1.value },
   NULL,
};
   
static const nir_search_variable replace247 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search248_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search248_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search248_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search248_0_0.value, &search248_0_1.value },
   NULL,
};

static const nir_search_variable search248_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search248_0.value, &search248_1.value },
   NULL,
};
   
static const nir_search_variable replace248_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace248_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace248_0.value, &replace248_1.value },
   NULL,
};
   
static const nir_search_variable search249_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search249_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search249_0_0.value },
   NULL,
};

static const nir_search_variable search249_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search249_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search249_1_0.value },
   NULL,
};
static const nir_search_expression search249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search249_0.value, &search249_1.value },
   NULL,
};
   
static const nir_search_variable replace249_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace249_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace249_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace249_0_0.value, &replace249_0_1.value },
   NULL,
};
static const nir_search_expression replace249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace249_0.value },
   NULL,
};
   
static const nir_search_constant search257_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search257_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search257_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression search257_1 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ushr,
   { &search257_1_0.value, &search257_1_1.value },
   NULL,
};
static const nir_search_expression search257 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search257_0.value, &search257_1.value },
   NULL,
};
   
static const nir_search_variable replace257_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace257_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression replace257 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace257_0.value, &replace257_1.value },
   NULL,
};
   
static const nir_search_constant search258_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

static const nir_search_variable search258_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search258_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search258_1 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ushr,
   { &search258_1_0.value, &search258_1_1.value },
   NULL,
};
static const nir_search_expression search258 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search258_0.value, &search258_1.value },
   NULL,
};
   
static const nir_search_variable replace258_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace258_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace258 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace258_0.value, &replace258_1.value },
   NULL,
};
   
static const nir_search_constant search315_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search315_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search315_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search315_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search315_1_0.value, &search315_1_1.value },
   NULL,
};
static const nir_search_expression search315 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search315_0.value, &search315_1.value },
   NULL,
};
   
static const nir_search_variable replace315_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace315_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace315 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace315_0.value, &replace315_1.value },
   NULL,
};
   
static const nir_search_constant search316_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search316_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search316_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search316_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search316_1_0.value, &search316_1_1.value },
   NULL,
};
static const nir_search_expression search316 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search316_0.value, &search316_1.value },
   NULL,
};
   
static const nir_search_variable replace316_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace316_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace316 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace316_0.value, &replace316_1.value },
   NULL,
};
   
static const nir_search_constant search317_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search317_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search317 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search317_0.value, &search317_1.value },
   NULL,
};
   
static const nir_search_variable replace317_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace317_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace317 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace317_0.value, &replace317_1.value },
   NULL,
};
   
static const nir_search_constant search319_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

static const nir_search_variable search319_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search319 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search319_0.value, &search319_1.value },
   NULL,
};
   
static const nir_search_variable replace319_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace319_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace319 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace319_0.value, &replace319_1.value },
   NULL,
};
   
static const nir_search_variable search399_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search399_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search399_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search399_0_0.value, &search399_0_1.value },
   NULL,
};

static const nir_search_variable search399_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search399_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search399_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search399_1_0.value, &search399_1_1.value },
   NULL,
};
static const nir_search_expression search399 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search399_0.value, &search399_1.value },
   NULL,
};
   
static const nir_search_variable replace399_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace399_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace399 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace399_0.value, &replace399_1.value },
   NULL,
};
   
static const nir_search_variable search400_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search400_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search400_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search400_0_0.value, &search400_0_1.value },
   NULL,
};

static const nir_search_variable search400_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search400_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search400_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search400_1_0.value, &search400_1_1.value },
   NULL,
};
static const nir_search_expression search400 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search400_0.value, &search400_1.value },
   NULL,
};
   
static const nir_search_variable replace400_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace400_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace400 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace400_0.value, &replace400_1.value },
   NULL,
};
   
static const nir_search_variable search401_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search401_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search401_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search401_0_0.value, &search401_0_1.value },
   NULL,
};

static const nir_search_variable search401_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search401_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search401_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search401_1_0.value, &search401_1_1.value },
   NULL,
};
static const nir_search_expression search401 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search401_0.value, &search401_1.value },
   NULL,
};
   
static const nir_search_variable replace401_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace401_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace401 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace401_0.value, &replace401_1.value },
   NULL,
};
   
static const nir_search_variable search402_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search402_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search402_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search402_0_0.value, &search402_0_1.value },
   NULL,
};

static const nir_search_variable search402_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search402_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search402_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search402_1_0.value, &search402_1_1.value },
   NULL,
};
static const nir_search_expression search402 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search402_0.value, &search402_1.value },
   NULL,
};
   
static const nir_search_variable replace402_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace402_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace402 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace402_0.value, &replace402_1.value },
   NULL,
};
   
static const nir_search_variable search403_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search403_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search403_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search403_0_0.value, &search403_0_1.value },
   NULL,
};

static const nir_search_variable search403_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search403_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search403_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search403_1_0.value, &search403_1_1.value },
   NULL,
};
static const nir_search_expression search403 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search403_0.value, &search403_1.value },
   NULL,
};
   
static const nir_search_variable replace403_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace403_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace403 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace403_0.value, &replace403_1.value },
   NULL,
};
   
static const nir_search_variable search404_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search404_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search404_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search404_0_0.value, &search404_0_1.value },
   NULL,
};

static const nir_search_variable search404_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search404_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search404_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search404_1_0.value, &search404_1_1.value },
   NULL,
};
static const nir_search_expression search404 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search404_0.value, &search404_1.value },
   NULL,
};
   
static const nir_search_variable replace404_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace404_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace404 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace404_0.value, &replace404_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search144, &replace144.value, 0 },
   { &search145, &replace145.value, 0 },
   { &search146, &replace146.value, 0 },
   { &search147, &replace147.value, 0 },
   { &search148, &replace148.value, 0 },
   { &search149, &replace149.value, 0 },
   { &search150, &replace150.value, 0 },
   { &search151, &replace151.value, 0 },
   { &search160, &replace160.value, 0 },
   { &search161, &replace161.value, 0 },
   { &search162, &replace162.value, 0 },
   { &search163, &replace163.value, 0 },
   { &search164, &replace164.value, 0 },
   { &search165, &replace165.value, 0 },
   { &search166, &replace166.value, 0 },
   { &search167, &replace167.value, 0 },
   { &search170, &replace170.value, 0 },
   { &search224, &replace224.value, 12 },
   { &search235, &replace235.value, 0 },
   { &search236, &replace236.value, 0 },
   { &search237, &replace237.value, 0 },
   { &search247, &replace247.value, 0 },
   { &search248, &replace248.value, 0 },
   { &search249, &replace249.value, 0 },
   { &search257, &replace257.value, 0 },
   { &search258, &replace258.value, 0 },
   { &search315, &replace315.value, 18 },
   { &search316, &replace316.value, 18 },
   { &search317, &replace317.value, 18 },
   { &search319, &replace319.value, 19 },
   { &search399, &replace399.value, 0 },
   { &search400, &replace400.value, 0 },
   { &search401, &replace401.value, 0 },
   { &search402, &replace402.value, 0 },
   { &search403, &replace403.value, 0 },
   { &search404, &replace404.value, 0 },
};
   
static const nir_search_variable search179_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search179_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search179_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search179_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search179_1_0.value, &search179_1_1.value },
   NULL,
};
static const nir_search_expression search179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search179_0.value, &search179_1.value },
   NULL,
};
   
static const nir_search_variable replace179_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace179_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace179_0.value, &replace179_1.value },
   NULL,
};
   
static const nir_search_variable search180_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search180_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search180_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search180_0_0.value, &search180_0_1.value },
   NULL,
};

static const nir_search_variable search180_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search180_0.value, &search180_1.value },
   NULL,
};
   
static const nir_search_variable replace180_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace180_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace180_0.value, &replace180_1.value },
   NULL,
};
   
static const nir_search_variable search187_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search187_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search187_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search187_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search187_1_0.value, &search187_1_1.value },
   NULL,
};
static const nir_search_expression search187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search187_0.value, &search187_1.value },
   NULL,
};
   
static const nir_search_constant replace187 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search188_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search188_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search188_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search188_0_0.value, &search188_0_1.value },
   NULL,
};

static const nir_search_variable search188_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search188 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search188_0.value, &search188_1.value },
   NULL,
};
   
static const nir_search_constant replace188 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search195_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search195_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search195_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search195_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search195_1_0.value, &search195_1_1.value },
   NULL,
};
static const nir_search_expression search195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search195_0.value, &search195_1.value },
   NULL,
};
   
static const nir_search_variable replace195_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace195_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace195_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace195_0_0.value, &replace195_0_1.value },
   NULL,
};

static const nir_search_variable replace195_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace195_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace195_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace195_1_0.value, &replace195_1_1.value },
   NULL,
};
static const nir_search_expression replace195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace195_0.value, &replace195_1.value },
   NULL,
};
   
static const nir_search_variable search196_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search196_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search196_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search196_0_0.value, &search196_0_1.value },
   NULL,
};

static const nir_search_variable search196_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search196_0.value, &search196_1.value },
   NULL,
};
   
static const nir_search_variable replace196_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace196_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace196_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace196_0_0.value, &replace196_0_1.value },
   NULL,
};

static const nir_search_variable replace196_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace196_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace196_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace196_1_0.value, &replace196_1_1.value },
   NULL,
};
static const nir_search_expression replace196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace196_0.value, &replace196_1.value },
   NULL,
};
   
static const nir_search_variable search203_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search203_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search203_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search203_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search203_1_0.value, &search203_1_1.value },
   NULL,
};
static const nir_search_expression search203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search203_0.value, &search203_1.value },
   NULL,
};
   
static const nir_search_variable replace203_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace203_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace203_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace203_0_0.value, &replace203_0_1.value },
   NULL,
};

static const nir_search_variable replace203_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace203_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace203_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace203_1_0.value, &replace203_1_1.value },
   NULL,
};
static const nir_search_expression replace203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace203_0.value, &replace203_1.value },
   NULL,
};
   
static const nir_search_variable search204_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search204_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search204_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search204_0_0.value, &search204_0_1.value },
   NULL,
};

static const nir_search_variable search204_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search204_0.value, &search204_1.value },
   NULL,
};
   
static const nir_search_variable replace204_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace204_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace204_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace204_0_0.value, &replace204_0_1.value },
   NULL,
};

static const nir_search_variable replace204_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace204_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace204_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace204_1_0.value, &replace204_1_1.value },
   NULL,
};
static const nir_search_expression replace204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace204_0.value, &replace204_1.value },
   NULL,
};
   
static const nir_search_variable search228_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search228_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search228_0.value, &search228_1.value },
   NULL,
};
   
static const nir_search_constant replace228 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search413_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search413_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search413_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search413_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search413_0_0.value, &search413_0_1.value, &search413_0_2.value },
   NULL,
};

static const nir_search_variable search413_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search413 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search413_0.value, &search413_1.value },
   NULL,
};
   
static const nir_search_variable replace413_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace413_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace413_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace413_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace413_1_0.value, &replace413_1_1.value },
   NULL,
};

static const nir_search_variable replace413_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace413_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace413_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace413_2_0.value, &replace413_2_1.value },
   NULL,
};
static const nir_search_expression replace413 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace413_0.value, &replace413_1.value, &replace413_2.value },
   NULL,
};
   
static const nir_search_variable search414_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search414_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search414_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search414_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search414_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search414_1_0.value, &search414_1_1.value, &search414_1_2.value },
   NULL,
};
static const nir_search_expression search414 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search414_0.value, &search414_1.value },
   NULL,
};
   
static const nir_search_variable replace414_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace414_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace414_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace414_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace414_1_0.value, &replace414_1_1.value },
   NULL,
};

static const nir_search_variable replace414_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace414_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace414_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace414_2_0.value, &replace414_2_1.value },
   NULL,
};
static const nir_search_expression replace414 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace414_0.value, &replace414_1.value, &replace414_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search179, &replace179.value, 0 },
   { &search180, &replace180.value, 0 },
   { &search187, &replace187.value, 0 },
   { &search188, &replace188.value, 0 },
   { &search195, &replace195.value, 0 },
   { &search196, &replace196.value, 0 },
   { &search203, &replace203.value, 0 },
   { &search204, &replace204.value, 0 },
   { &search228, &replace228.value, 0 },
   { &search413, &replace413.value, 0 },
   { &search414, &replace414.value, 0 },
};
   
static const nir_search_variable search181_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search181_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search181_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search181_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search181_1_0.value, &search181_1_1.value },
   NULL,
};
static const nir_search_expression search181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search181_0.value, &search181_1.value },
   NULL,
};
   
static const nir_search_constant replace181 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search182_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search182_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search182_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search182_0_0.value, &search182_0_1.value },
   NULL,
};

static const nir_search_variable search182_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search182_0.value, &search182_1.value },
   NULL,
};
   
static const nir_search_constant replace182 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search189_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search189_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search189_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search189_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search189_1_0.value, &search189_1_1.value },
   NULL,
};
static const nir_search_expression search189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search189_0.value, &search189_1.value },
   NULL,
};
   
static const nir_search_variable replace189_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace189_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace189_0.value, &replace189_1.value },
   NULL,
};
   
static const nir_search_variable search190_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search190_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search190_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search190_0_0.value, &search190_0_1.value },
   NULL,
};

static const nir_search_variable search190_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search190_0.value, &search190_1.value },
   NULL,
};
   
static const nir_search_variable replace190_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace190_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace190_0.value, &replace190_1.value },
   NULL,
};
   
static const nir_search_variable search197_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search197_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search197_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search197_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search197_1_0.value, &search197_1_1.value },
   NULL,
};
static const nir_search_expression search197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search197_0.value, &search197_1.value },
   NULL,
};
   
static const nir_search_variable replace197_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace197_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace197_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace197_0_0.value, &replace197_0_1.value },
   NULL,
};

static const nir_search_variable replace197_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace197_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace197_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace197_1_0.value, &replace197_1_1.value },
   NULL,
};
static const nir_search_expression replace197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace197_0.value, &replace197_1.value },
   NULL,
};
   
static const nir_search_variable search198_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search198_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search198_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search198_0_0.value, &search198_0_1.value },
   NULL,
};

static const nir_search_variable search198_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search198_0.value, &search198_1.value },
   NULL,
};
   
static const nir_search_variable replace198_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace198_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace198_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace198_0_0.value, &replace198_0_1.value },
   NULL,
};

static const nir_search_variable replace198_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace198_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace198_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace198_1_0.value, &replace198_1_1.value },
   NULL,
};
static const nir_search_expression replace198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace198_0.value, &replace198_1.value },
   NULL,
};
   
static const nir_search_variable search205_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search205_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search205_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search205_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search205_1_0.value, &search205_1_1.value },
   NULL,
};
static const nir_search_expression search205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search205_0.value, &search205_1.value },
   NULL,
};
   
static const nir_search_variable replace205_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace205_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace205_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace205_0_0.value, &replace205_0_1.value },
   NULL,
};

static const nir_search_variable replace205_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace205_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace205_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace205_1_0.value, &replace205_1_1.value },
   NULL,
};
static const nir_search_expression replace205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace205_0.value, &replace205_1.value },
   NULL,
};
   
static const nir_search_variable search206_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search206_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search206_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search206_0_0.value, &search206_0_1.value },
   NULL,
};

static const nir_search_variable search206_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search206_0.value, &search206_1.value },
   NULL,
};
   
static const nir_search_variable replace206_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace206_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace206_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace206_0_0.value, &replace206_0_1.value },
   NULL,
};

static const nir_search_variable replace206_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace206_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace206_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace206_1_0.value, &replace206_1_1.value },
   NULL,
};
static const nir_search_expression replace206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace206_0.value, &replace206_1.value },
   NULL,
};
   
static const nir_search_variable search229_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search229_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search229_0.value, &search229_1.value },
   NULL,
};
   
static const nir_search_constant replace229 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search415_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search415_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search415_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search415_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search415_0_0.value, &search415_0_1.value, &search415_0_2.value },
   NULL,
};

static const nir_search_variable search415_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search415 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search415_0.value, &search415_1.value },
   NULL,
};
   
static const nir_search_variable replace415_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace415_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace415_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace415_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace415_1_0.value, &replace415_1_1.value },
   NULL,
};

static const nir_search_variable replace415_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace415_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace415_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace415_2_0.value, &replace415_2_1.value },
   NULL,
};
static const nir_search_expression replace415 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace415_0.value, &replace415_1.value, &replace415_2.value },
   NULL,
};
   
static const nir_search_variable search416_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search416_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search416_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search416_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search416_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search416_1_0.value, &search416_1_1.value, &search416_1_2.value },
   NULL,
};
static const nir_search_expression search416 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search416_0.value, &search416_1.value },
   NULL,
};
   
static const nir_search_variable replace416_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace416_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace416_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace416_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace416_1_0.value, &replace416_1_1.value },
   NULL,
};

static const nir_search_variable replace416_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace416_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace416_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace416_2_0.value, &replace416_2_1.value },
   NULL,
};
static const nir_search_expression replace416 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace416_0.value, &replace416_1.value, &replace416_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search181, &replace181.value, 0 },
   { &search182, &replace182.value, 0 },
   { &search189, &replace189.value, 0 },
   { &search190, &replace190.value, 0 },
   { &search197, &replace197.value, 0 },
   { &search198, &replace198.value, 0 },
   { &search205, &replace205.value, 0 },
   { &search206, &replace206.value, 0 },
   { &search229, &replace229.value, 0 },
   { &search415, &replace415.value, 0 },
   { &search416, &replace416.value, 0 },
};
   
static const nir_search_variable search183_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search183_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search183_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search183_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search183_1_0.value, &search183_1_1.value },
   NULL,
};
static const nir_search_expression search183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search183_0.value, &search183_1.value },
   NULL,
};
   
static const nir_search_variable replace183_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace183_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace183_0.value, &replace183_1.value },
   NULL,
};
   
static const nir_search_variable search184_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search184_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search184_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search184_0_0.value, &search184_0_1.value },
   NULL,
};

static const nir_search_variable search184_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search184_0.value, &search184_1.value },
   NULL,
};
   
static const nir_search_variable replace184_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace184_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace184_0.value, &replace184_1.value },
   NULL,
};
   
static const nir_search_variable search191_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search191_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search191_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search191_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search191_1_0.value, &search191_1_1.value },
   NULL,
};
static const nir_search_expression search191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search191_0.value, &search191_1.value },
   NULL,
};
   
static const nir_search_constant replace191 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search192_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search192_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search192_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search192_0_0.value, &search192_0_1.value },
   NULL,
};

static const nir_search_variable search192_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search192_0.value, &search192_1.value },
   NULL,
};
   
static const nir_search_constant replace192 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search199_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search199_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search199_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search199_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search199_1_0.value, &search199_1_1.value },
   NULL,
};
static const nir_search_expression search199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search199_0.value, &search199_1.value },
   NULL,
};
   
static const nir_search_variable replace199_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace199_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace199_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace199_0_0.value, &replace199_0_1.value },
   NULL,
};

static const nir_search_variable replace199_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace199_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace199_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace199_1_0.value, &replace199_1_1.value },
   NULL,
};
static const nir_search_expression replace199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace199_0.value, &replace199_1.value },
   NULL,
};
   
static const nir_search_variable search200_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search200_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search200_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search200_0_0.value, &search200_0_1.value },
   NULL,
};

static const nir_search_variable search200_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search200_0.value, &search200_1.value },
   NULL,
};
   
static const nir_search_variable replace200_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace200_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace200_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace200_0_0.value, &replace200_0_1.value },
   NULL,
};

static const nir_search_variable replace200_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace200_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace200_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace200_1_0.value, &replace200_1_1.value },
   NULL,
};
static const nir_search_expression replace200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace200_0.value, &replace200_1.value },
   NULL,
};
   
static const nir_search_variable search207_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search207_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search207_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search207_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search207_1_0.value, &search207_1_1.value },
   NULL,
};
static const nir_search_expression search207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search207_0.value, &search207_1.value },
   NULL,
};
   
static const nir_search_variable replace207_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace207_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace207_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace207_0_0.value, &replace207_0_1.value },
   NULL,
};

static const nir_search_variable replace207_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace207_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace207_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace207_1_0.value, &replace207_1_1.value },
   NULL,
};
static const nir_search_expression replace207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace207_0.value, &replace207_1.value },
   NULL,
};
   
static const nir_search_variable search208_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search208_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search208_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search208_0_0.value, &search208_0_1.value },
   NULL,
};

static const nir_search_variable search208_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search208_0.value, &search208_1.value },
   NULL,
};
   
static const nir_search_variable replace208_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace208_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace208_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace208_0_0.value, &replace208_0_1.value },
   NULL,
};

static const nir_search_variable replace208_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace208_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace208_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace208_1_0.value, &replace208_1_1.value },
   NULL,
};
static const nir_search_expression replace208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace208_0.value, &replace208_1.value },
   NULL,
};
   
static const nir_search_variable search232_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search232_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search232_0.value, &search232_1.value },
   NULL,
};
   
static const nir_search_constant replace232 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search421_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search421_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search421_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search421_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search421_0_0.value, &search421_0_1.value, &search421_0_2.value },
   NULL,
};

static const nir_search_variable search421_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search421 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search421_0.value, &search421_1.value },
   NULL,
};
   
static const nir_search_variable replace421_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace421_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace421_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace421_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace421_1_0.value, &replace421_1_1.value },
   NULL,
};

static const nir_search_variable replace421_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace421_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace421_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace421_2_0.value, &replace421_2_1.value },
   NULL,
};
static const nir_search_expression replace421 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace421_0.value, &replace421_1.value, &replace421_2.value },
   NULL,
};
   
static const nir_search_variable search422_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search422_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search422_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search422_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search422_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search422_1_0.value, &search422_1_1.value, &search422_1_2.value },
   NULL,
};
static const nir_search_expression search422 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search422_0.value, &search422_1.value },
   NULL,
};
   
static const nir_search_variable replace422_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace422_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace422_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace422_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace422_1_0.value, &replace422_1_1.value },
   NULL,
};

static const nir_search_variable replace422_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace422_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace422_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace422_2_0.value, &replace422_2_1.value },
   NULL,
};
static const nir_search_expression replace422 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace422_0.value, &replace422_1.value, &replace422_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search183, &replace183.value, 0 },
   { &search184, &replace184.value, 0 },
   { &search191, &replace191.value, 0 },
   { &search192, &replace192.value, 0 },
   { &search199, &replace199.value, 0 },
   { &search200, &replace200.value, 0 },
   { &search207, &replace207.value, 0 },
   { &search208, &replace208.value, 0 },
   { &search232, &replace232.value, 0 },
   { &search421, &replace421.value, 0 },
   { &search422, &replace422.value, 0 },
};
   
static const nir_search_variable search185_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search185_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search185_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search185_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search185_1_0.value, &search185_1_1.value },
   NULL,
};
static const nir_search_expression search185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search185_0.value, &search185_1.value },
   NULL,
};
   
static const nir_search_constant replace185 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search186_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search186_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search186_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search186_0_0.value, &search186_0_1.value },
   NULL,
};

static const nir_search_variable search186_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search186_0.value, &search186_1.value },
   NULL,
};
   
static const nir_search_constant replace186 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search193_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search193_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search193_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search193_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search193_1_0.value, &search193_1_1.value },
   NULL,
};
static const nir_search_expression search193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search193_0.value, &search193_1.value },
   NULL,
};
   
static const nir_search_variable replace193_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace193_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace193_0.value, &replace193_1.value },
   NULL,
};
   
static const nir_search_variable search194_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search194_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search194_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search194_0_0.value, &search194_0_1.value },
   NULL,
};

static const nir_search_variable search194_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search194_0.value, &search194_1.value },
   NULL,
};
   
static const nir_search_variable replace194_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace194_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace194_0.value, &replace194_1.value },
   NULL,
};
   
static const nir_search_variable search201_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search201_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search201_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search201_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search201_1_0.value, &search201_1_1.value },
   NULL,
};
static const nir_search_expression search201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search201_0.value, &search201_1.value },
   NULL,
};
   
static const nir_search_variable replace201_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace201_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace201_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace201_0_0.value, &replace201_0_1.value },
   NULL,
};

static const nir_search_variable replace201_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace201_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace201_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace201_1_0.value, &replace201_1_1.value },
   NULL,
};
static const nir_search_expression replace201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace201_0.value, &replace201_1.value },
   NULL,
};
   
static const nir_search_variable search202_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search202_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search202_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search202_0_0.value, &search202_0_1.value },
   NULL,
};

static const nir_search_variable search202_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search202_0.value, &search202_1.value },
   NULL,
};
   
static const nir_search_variable replace202_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace202_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace202_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace202_0_0.value, &replace202_0_1.value },
   NULL,
};

static const nir_search_variable replace202_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace202_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace202_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace202_1_0.value, &replace202_1_1.value },
   NULL,
};
static const nir_search_expression replace202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace202_0.value, &replace202_1.value },
   NULL,
};
   
static const nir_search_variable search209_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search209_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search209_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search209_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search209_1_0.value, &search209_1_1.value },
   NULL,
};
static const nir_search_expression search209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search209_0.value, &search209_1.value },
   NULL,
};
   
static const nir_search_variable replace209_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace209_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace209_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace209_0_0.value, &replace209_0_1.value },
   NULL,
};

static const nir_search_variable replace209_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace209_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace209_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace209_1_0.value, &replace209_1_1.value },
   NULL,
};
static const nir_search_expression replace209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace209_0.value, &replace209_1.value },
   NULL,
};
   
static const nir_search_variable search210_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search210_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search210_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search210_0_0.value, &search210_0_1.value },
   NULL,
};

static const nir_search_variable search210_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search210_0.value, &search210_1.value },
   NULL,
};
   
static const nir_search_variable replace210_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace210_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace210_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace210_0_0.value, &replace210_0_1.value },
   NULL,
};

static const nir_search_variable replace210_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace210_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace210_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace210_1_0.value, &replace210_1_1.value },
   NULL,
};
static const nir_search_expression replace210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace210_0.value, &replace210_1.value },
   NULL,
};
   
static const nir_search_variable search233_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search233_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search233_0.value, &search233_1.value },
   NULL,
};
   
static const nir_search_constant replace233 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search423_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search423_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search423_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search423_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search423_0_0.value, &search423_0_1.value, &search423_0_2.value },
   NULL,
};

static const nir_search_variable search423_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search423 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search423_0.value, &search423_1.value },
   NULL,
};
   
static const nir_search_variable replace423_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace423_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace423_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace423_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace423_1_0.value, &replace423_1_1.value },
   NULL,
};

static const nir_search_variable replace423_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace423_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace423_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace423_2_0.value, &replace423_2_1.value },
   NULL,
};
static const nir_search_expression replace423 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace423_0.value, &replace423_1.value, &replace423_2.value },
   NULL,
};
   
static const nir_search_variable search424_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search424_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search424_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search424_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search424_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search424_1_0.value, &search424_1_1.value, &search424_1_2.value },
   NULL,
};
static const nir_search_expression search424 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search424_0.value, &search424_1.value },
   NULL,
};
   
static const nir_search_variable replace424_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace424_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace424_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace424_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace424_1_0.value, &replace424_1_1.value },
   NULL,
};

static const nir_search_variable replace424_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace424_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace424_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace424_2_0.value, &replace424_2_1.value },
   NULL,
};
static const nir_search_expression replace424 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace424_0.value, &replace424_1.value, &replace424_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search185, &replace185.value, 0 },
   { &search186, &replace186.value, 0 },
   { &search193, &replace193.value, 0 },
   { &search194, &replace194.value, 0 },
   { &search201, &replace201.value, 0 },
   { &search202, &replace202.value, 0 },
   { &search209, &replace209.value, 0 },
   { &search210, &replace210.value, 0 },
   { &search233, &replace233.value, 0 },
   { &search423, &replace423.value, 0 },
   { &search424, &replace424.value, 0 },
};
   
static const nir_search_variable search215_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search215_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search215_0.value, &search215_1.value },
   NULL,
};
   
static const nir_search_variable replace215_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace215_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace215_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace215_0_0.value, &replace215_0_1.value },
   NULL,
};
static const nir_search_expression replace215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace215_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_slt_xforms[] = {
   { &search215, &replace215.value, 11 },
};
   
static const nir_search_variable search216_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search216_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search216_0.value, &search216_1.value },
   NULL,
};
   
static const nir_search_variable replace216_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace216_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace216_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace216_0_0.value, &replace216_0_1.value },
   NULL,
};
static const nir_search_expression replace216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace216_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_sge_xforms[] = {
   { &search216, &replace216.value, 11 },
};
   
static const nir_search_variable search217_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search217_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search217_0.value, &search217_1.value },
   NULL,
};
   
static const nir_search_variable replace217_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace217_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace217_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace217_0_0.value, &replace217_0_1.value },
   NULL,
};
static const nir_search_expression replace217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace217_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_seq_xforms[] = {
   { &search217, &replace217.value, 11 },
};
   
static const nir_search_variable search218_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search218_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search218_0.value, &search218_1.value },
   NULL,
};
   
static const nir_search_variable replace218_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace218_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace218_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace218_0_0.value, &replace218_0_1.value },
   NULL,
};
static const nir_search_expression replace218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace218_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_sne_xforms[] = {
   { &search218, &replace218.value, 11 },
};
   
static const nir_search_variable search234_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search234_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fand,
   { &search234_0.value, &search234_1.value },
   NULL,
};
   
static const nir_search_constant replace234 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search234, &replace234.value, 0 },
};
   
static const nir_search_variable search241_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search241_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fxor,
   { &search241_0.value, &search241_1.value },
   NULL,
};
   
static const nir_search_constant replace241 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search241, &replace241.value, 0 },
};
   
static const nir_search_variable search242_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search242_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search242_0.value, &search242_1.value },
   NULL,
};
   
static const nir_search_constant replace242 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search243_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search243_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search243_0.value, &search243_1.value },
   NULL,
};
   
static const nir_search_variable replace243 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search242, &replace242.value, 0 },
   { &search243, &replace243.value, 0 },
};
   
static const nir_search_constant search253_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search253_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search253 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search253_0.value, &search253_1.value },
   NULL,
};
   
static const nir_search_constant replace253 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search254_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search254_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search254 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search254_0.value, &search254_1.value },
   NULL,
};
   
static const nir_search_variable replace254 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search253, &replace253.value, 0 },
   { &search254, &replace254.value, 0 },
};
   
static const nir_search_constant search255_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search255_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search255 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search255_0.value, &search255_1.value },
   NULL,
};
   
static const nir_search_constant replace255 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search256_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search256_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search256 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search256_0.value, &search256_1.value },
   NULL,
};
   
static const nir_search_variable replace256 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search314_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search314_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression search314 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search314_0.value, &search314_1.value },
   NULL,
};
   
static const nir_search_variable replace314_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace314_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace314 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace314_0.value, &replace314_1.value },
   NULL,
};
   
static const nir_search_variable search318_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search318_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search318 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search318_0.value, &search318_1.value },
   NULL,
};
   
static const nir_search_variable replace318_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace318_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace318 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace318_0.value, &replace318_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search255, &replace255.value, 0 },
   { &search256, &replace256.value, 0 },
   { &search314, &replace314.value, 18 },
   { &search318, &replace318.value, 19 },
};
   
static const nir_search_variable search259_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search259_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search259_0_0.value },
   NULL,
};
static const nir_search_expression search259 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search259_0.value },
   NULL,
};
   
static const nir_search_variable replace259 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search262_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search262_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search262_0_0_0.value },
   NULL,
};

static const nir_search_variable search262_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search262_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search262_0_0.value, &search262_0_1.value },
   NULL,
};
static const nir_search_expression search262 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search262_0.value },
   NULL,
};
   
static const nir_search_variable replace262_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace262_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace262 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace262_0.value, &replace262_1.value },
   NULL,
};
   
static const nir_search_variable search263_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search263_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search263_0_0_0_0.value },
   NULL,
};

static const nir_search_variable search263_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search263_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search263_0_0_0.value, &search263_0_0_1.value },
   NULL,
};

static const nir_search_variable search263_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search263_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search263_0_1_0_0.value },
   NULL,
};

static const nir_search_variable search263_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search263_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search263_0_1_0.value, &search263_0_1_1.value },
   NULL,
};
static const nir_search_expression search263_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search263_0_0.value, &search263_0_1.value },
   NULL,
};
static const nir_search_expression search263 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search263_0.value },
   NULL,
};
   
static const nir_search_variable replace263_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace263_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace263_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace263_0_0.value, &replace263_0_1.value },
   NULL,
};

static const nir_search_variable replace263_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace263_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace263_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace263_1_0.value, &replace263_1_1.value },
   NULL,
};
static const nir_search_expression replace263 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &replace263_0.value, &replace263_1.value },
   NULL,
};
   
static const nir_search_variable search264_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search264_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search264_0_0_0.value },
   NULL,
};

static const nir_search_constant search264_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};
static const nir_search_expression search264_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search264_0_0.value, &search264_0_1.value },
   NULL,
};
static const nir_search_expression search264 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search264_0.value },
   NULL,
};
   
static const nir_search_variable replace264_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace264_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace264 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace264_0.value, &replace264_1.value },
   NULL,
};
   
static const nir_search_variable search265_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search265_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search265_0_0_0.value },
   NULL,
};

static const nir_search_constant search265_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4010000000000000 /* 4.0 */ },
};
static const nir_search_expression search265_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search265_0_0.value, &search265_0_1.value },
   NULL,
};
static const nir_search_expression search265 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search265_0.value },
   NULL,
};
   
static const nir_search_variable replace265_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace265_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace265_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace265_0_0.value, &replace265_0_1.value },
   NULL,
};

static const nir_search_variable replace265_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace265_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace265_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace265_1_0.value, &replace265_1_1.value },
   NULL,
};
static const nir_search_expression replace265 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace265_0.value, &replace265_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search259, &replace259.value, 0 },
   { &search262, &replace262.value, 14 },
   { &search263, &replace263.value, 14 },
   { &search264, &replace264.value, 0 },
   { &search265, &replace265.value, 0 },
};
   
static const nir_search_variable search260_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search260_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search260_0_0.value },
   NULL,
};
static const nir_search_expression search260 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search260_0.value },
   NULL,
};
   
static const nir_search_variable replace260 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search275_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search275_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search275_0_0.value },
   NULL,
};
static const nir_search_expression search275 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search275_0.value },
   NULL,
};
   
static const nir_search_constant replace275_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

static const nir_search_variable replace275_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace275_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace275_1_0.value },
   NULL,
};
static const nir_search_expression replace275 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace275_0.value, &replace275_1.value },
   NULL,
};
   
static const nir_search_variable search276_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search276_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search276_0_0.value },
   NULL,
};
static const nir_search_expression search276 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search276_0.value },
   NULL,
};
   
static const nir_search_variable replace276_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace276_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace276_0_0.value },
   NULL,
};
static const nir_search_expression replace276 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace276_0.value },
   NULL,
};
   
static const nir_search_variable search277_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search277_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search277_0_0.value },
   NULL,
};
static const nir_search_expression search277 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search277_0.value },
   NULL,
};
   
static const nir_search_constant replace277_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

static const nir_search_variable replace277_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace277_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace277_1_0.value },
   NULL,
};
static const nir_search_expression replace277 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace277_0.value, &replace277_1.value },
   NULL,
};
   
static const nir_search_variable search278_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search278_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search278_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search278_0_0.value, &search278_0_1.value },
   NULL,
};
static const nir_search_expression search278 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search278_0.value },
   NULL,
};
   
static const nir_search_variable replace278_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace278_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace278_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace278_1_0.value },
   NULL,
};
static const nir_search_expression replace278 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace278_0.value, &replace278_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search260, &replace260.value, 0 },
   { &search275, &replace275.value, 0 },
   { &search276, &replace276.value, 0 },
   { &search277, &replace277.value, 0 },
   { &search278, &replace278.value, 0 },
};
   
static const nir_search_variable search261_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search261_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search261 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search261_0.value, &search261_1.value },
   NULL,
};
   
static const nir_search_variable replace261_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace261_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace261_0_0_0.value },
   NULL,
};

static const nir_search_variable replace261_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace261_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace261_0_0.value, &replace261_0_1.value },
   NULL,
};
static const nir_search_expression replace261 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace261_0.value },
   NULL,
};
   
static const nir_search_variable search266_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search266_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search266 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search266_0.value, &search266_1.value },
   NULL,
};
   
static const nir_search_variable replace266 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search267_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search267_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};
static const nir_search_expression search267 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search267_0.value, &search267_1.value },
   NULL,
};
   
static const nir_search_variable replace267_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace267_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace267 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace267_0.value, &replace267_1.value },
   NULL,
};
   
static const nir_search_variable search268_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search268_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4010000000000000 /* 4.0 */ },
};
static const nir_search_expression search268 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search268_0.value, &search268_1.value },
   NULL,
};
   
static const nir_search_variable replace268_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace268_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace268_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace268_0_0.value, &replace268_0_1.value },
   NULL,
};

static const nir_search_variable replace268_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace268_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace268_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace268_1_0.value, &replace268_1_1.value },
   NULL,
};
static const nir_search_expression replace268 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace268_0.value, &replace268_1.value },
   NULL,
};
   
static const nir_search_constant search269_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};

static const nir_search_variable search269_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search269 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search269_0.value, &search269_1.value },
   NULL,
};
   
static const nir_search_variable replace269_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace269 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace269_0.value },
   NULL,
};
   
static const nir_search_variable search270_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search270_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
static const nir_search_expression search270_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search270_0_0.value, &search270_0_1.value },
   NULL,
};

static const nir_search_constant search270_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
static const nir_search_expression search270 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search270_0.value, &search270_1.value },
   NULL,
};
   
static const nir_search_variable replace270 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search271_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search271_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
static const nir_search_expression search271_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search271_0_0_0.value, &search271_0_0_1.value },
   NULL,
};
static const nir_search_expression search271_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search271_0_0.value },
   NULL,
};

static const nir_search_constant search271_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
static const nir_search_expression search271 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search271_0.value, &search271_1.value },
   NULL,
};
   
static const nir_search_variable replace271_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace271 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace271_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search261, &replace261.value, 13 },
   { &search266, &replace266.value, 0 },
   { &search267, &replace267.value, 0 },
   { &search268, &replace268.value, 0 },
   { &search269, &replace269.value, 0 },
   { &search270, &replace270.value, 0 },
   { &search271, &replace271.value, 0 },
};
   
static const nir_search_variable search272_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search272_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search272_0_0.value },
   NULL,
};
static const nir_search_expression search272 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsqrt,
   { &search272_0.value },
   NULL,
};
   
static const nir_search_constant replace272_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

static const nir_search_variable replace272_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace272_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace272_0_0.value, &replace272_0_1.value },
   NULL,
};
static const nir_search_expression replace272 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace272_0.value },
   NULL,
};
   
static const nir_search_variable search284_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search284 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search284_0.value },
   NULL,
};
   
static const nir_search_variable replace284_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace284_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace284_0_0.value },
   NULL,
};
static const nir_search_expression replace284 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace284_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsqrt_xforms[] = {
   { &search272, &replace272.value, 0 },
   { &search284, &replace284.value, 16 },
};
   
static const nir_search_variable search273_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search273_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search273_0_0.value },
   NULL,
};
static const nir_search_expression search273 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search273_0.value },
   NULL,
};
   
static const nir_search_variable replace273_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace273_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace273_0_0.value },
   NULL,
};
static const nir_search_expression replace273 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace273_0.value },
   NULL,
};
   
static const nir_search_variable search282_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search282_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search282_0_0.value },
   NULL,
};
static const nir_search_expression search282 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search282_0.value },
   NULL,
};
   
static const nir_search_variable replace282 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search283_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search283_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search283_0_0.value },
   NULL,
};
static const nir_search_expression search283 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search283_0.value },
   NULL,
};
   
static const nir_search_variable replace283_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace283 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace283_0.value },
   NULL,
};
   
static const nir_search_variable search285_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search285_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search285_0_0.value },
   NULL,
};
static const nir_search_expression search285 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search285_0.value },
   NULL,
};
   
static const nir_search_variable replace285_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace285 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &replace285_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search273, &replace273.value, 0 },
   { &search282, &replace282.value, 0 },
   { &search283, &replace283.value, 0 },
   { &search285, &replace285.value, 17 },
};
   
static const nir_search_variable search274_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search274_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search274_0_0.value },
   NULL,
};
static const nir_search_expression search274 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frsq,
   { &search274_0.value },
   NULL,
};
   
static const nir_search_constant replace274_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

static const nir_search_variable replace274_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace274_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace274_0_0.value, &replace274_0_1.value },
   NULL,
};
static const nir_search_expression replace274 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace274_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frsq_xforms[] = {
   { &search274, &replace274.value, 0 },
};
   
static const nir_search_constant search280_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search280_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search280 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fdiv,
   { &search280_0.value, &search280_1.value },
   NULL,
};
   
static const nir_search_variable replace280_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace280 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace280_0.value },
   NULL,
};
   
static const nir_search_variable search281_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search281_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search281 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &search281_0.value, &search281_1.value },
   NULL,
};
   
static const nir_search_variable replace281_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace281_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace281_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace281_1_0.value },
   NULL,
};
static const nir_search_expression replace281 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace281_0.value, &replace281_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search280, &replace280.value, 0 },
   { &search281, &replace281.value, 15 },
};
   
static const nir_search_variable search301_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search301_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search301_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search301 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fcsel,
   { &search301_0.value, &search301_1.value, &search301_2.value },
   NULL,
};
   
static const nir_search_variable replace301 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_fcsel_xforms[] = {
   { &search301, &replace301.value, 0 },
};
   
static const nir_search_variable search302_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search302_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search302_0_0.value },
   NULL,
};
static const nir_search_expression search302 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search302_0.value },
   NULL,
};
   
static const nir_search_variable replace302 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search303_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};
static const nir_search_expression search303 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search303_0.value },
   NULL,
};
   
static const nir_search_variable replace303 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search306_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search306_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search306_0_0.value },
   NULL,
};
static const nir_search_expression search306 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search306_0.value },
   NULL,
};
   
static const nir_search_variable replace306_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace306 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace306_0.value },
   NULL,
};
   
static const nir_search_variable search307_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search307_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search307_0_0.value },
   NULL,
};
static const nir_search_expression search307 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search307_0.value },
   NULL,
};
   
static const nir_search_variable replace307_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace307 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace307_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_i2b_xforms[] = {
   { &search302, &replace302.value, 0 },
   { &search303, &replace303.value, 0 },
   { &search306, &replace306.value, 0 },
   { &search307, &replace307.value, 0 },
};
   
static const nir_search_variable search304_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search304_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search304_0_0.value },
   NULL,
};
static const nir_search_expression search304 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &search304_0.value },
   NULL,
};
   
static const nir_search_variable replace304_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace304 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &replace304_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_f2i32_xforms[] = {
   { &search304, &replace304.value, 0 },
};
   
static const nir_search_variable search305_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search305_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search305_0_0.value },
   NULL,
};
static const nir_search_expression search305 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &search305_0.value },
   NULL,
};
   
static const nir_search_variable replace305_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace305 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &replace305_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_f2u32_xforms[] = {
   { &search305, &replace305.value, 0 },
};
   
static const nir_search_variable search311_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search311_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search311_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &search311_0_0.value, &search311_0_1.value },
   NULL,
};
static const nir_search_expression search311 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_x,
   { &search311_0.value },
   NULL,
};
   
static const nir_search_variable replace311 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_64_2x32_split_x_xforms[] = {
   { &search311, &replace311.value, 0 },
};
   
static const nir_search_variable search312_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search312_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search312_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &search312_0_0.value, &search312_0_1.value },
   NULL,
};
static const nir_search_expression search312 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_y,
   { &search312_0.value },
   NULL,
};
   
static const nir_search_variable replace312 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_64_2x32_split_y_xforms[] = {
   { &search312, &replace312.value, 0 },
};
   
static const nir_search_variable search313_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search313_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_x,
   { &search313_0_0.value },
   NULL,
};

static const nir_search_variable search313_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search313_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_y,
   { &search313_1_0.value },
   NULL,
};
static const nir_search_expression search313 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &search313_0.value, &search313_1.value },
   NULL,
};
   
static const nir_search_variable replace313 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_pack_64_2x32_split_xforms[] = {
   { &search313, &replace313.value, 0 },
};
   
static const nir_search_variable search320_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search320_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search320_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search320_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search320_1_0.value, &search320_1_1.value },
   NULL,
};
static const nir_search_expression search320 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsub,
   { &search320_0.value, &search320_1.value },
   NULL,
};
   
static const nir_search_variable replace320_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace320_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace320 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace320_0.value, &replace320_1.value },
   NULL,
};
   
static const nir_search_variable search324_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search324_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search324 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search324_0.value, &search324_1.value },
   NULL,
};
   
static const nir_search_variable replace324_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace324_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace324_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace324_1_0.value },
   NULL,
};
static const nir_search_expression replace324 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace324_0.value, &replace324_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsub_xforms[] = {
   { &search320, &replace320.value, 0 },
   { &search324, &replace324.value, 20 },
};
   
static const nir_search_variable search321_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search321_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search321_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search321_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search321_1_0.value, &search321_1_1.value },
   NULL,
};
static const nir_search_expression search321 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search321_0.value, &search321_1.value },
   NULL,
};
   
static const nir_search_variable replace321_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace321_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace321 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace321_0.value, &replace321_1.value },
   NULL,
};
   
static const nir_search_variable search325_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search325_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search325 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search325_0.value, &search325_1.value },
   NULL,
};
   
static const nir_search_variable replace325_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace325_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace325_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace325_1_0.value },
   NULL,
};
static const nir_search_expression replace325 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace325_0.value, &replace325_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_isub_xforms[] = {
   { &search321, &replace321.value, 0 },
   { &search325, &replace325.value, 20 },
};
   
static const nir_search_variable search322_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search322_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search322 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search322_0.value, &search322_1.value },
   NULL,
};
   
static const nir_search_variable replace322 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search323_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search323_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search323 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search323_0.value, &search323_1.value },
   NULL,
};
   
static const nir_search_constant replace323 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ussub_4x8_xforms[] = {
   { &search322, &replace322.value, 0 },
   { &search323, &replace323.value, 0 },
};
   
static const nir_search_variable search350_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search350_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search350 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_fmod,
   { &search350_0.value, &search350_1.value },
   NULL,
};
   
static const nir_search_variable replace350_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace350_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace350_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace350_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace350_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace350_1_1_0_0.value, &replace350_1_1_0_1.value },
   NULL,
};
static const nir_search_expression replace350_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace350_1_1_0.value },
   NULL,
};
static const nir_search_expression replace350_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace350_1_0.value, &replace350_1_1.value },
   NULL,
};
static const nir_search_expression replace350 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace350_0.value, &replace350_1.value },
   NULL,
};
   
static const nir_search_variable search351_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search351_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search351 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_fmod,
   { &search351_0.value, &search351_1.value },
   NULL,
};
   
static const nir_search_variable replace351_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace351_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace351_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace351_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace351_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace351_1_1_0_0.value, &replace351_1_1_0_1.value },
   NULL,
};
static const nir_search_expression replace351_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace351_1_1_0.value },
   NULL,
};
static const nir_search_expression replace351_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace351_1_0.value, &replace351_1_1.value },
   NULL,
};
static const nir_search_expression replace351 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace351_0.value, &replace351_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmod_xforms[] = {
   { &search350, &replace350.value, 22 },
   { &search351, &replace351.value, 23 },
};
   
static const nir_search_variable search352_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search352_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search352 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frem,
   { &search352_0.value, &search352_1.value },
   NULL,
};
   
static const nir_search_variable replace352_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace352_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace352_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace352_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace352_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace352_1_1_0_0.value, &replace352_1_1_0_1.value },
   NULL,
};
static const nir_search_expression replace352_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &replace352_1_1_0.value },
   NULL,
};
static const nir_search_expression replace352_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace352_1_0.value, &replace352_1_1.value },
   NULL,
};
static const nir_search_expression replace352 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace352_0.value, &replace352_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frem_xforms[] = {
   { &search352, &replace352.value, 22 },
};
   
static const nir_search_variable search353_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search353_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search353 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_uadd_carry,
   { &search353_0.value, &search353_1.value },
   NULL,
};
   
static const nir_search_variable replace353_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace353_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace353_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace353_0_0_0.value, &replace353_0_0_1.value },
   NULL,
};

static const nir_search_variable replace353_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace353_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace353_0_0.value, &replace353_0_1.value },
   NULL,
};
static const nir_search_expression replace353 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace353_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_uadd_carry_xforms[] = {
   { &search353, &replace353.value, 24 },
};
   
static const nir_search_variable search354_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search354_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search354 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_usub_borrow,
   { &search354_0.value, &search354_1.value },
   NULL,
};
   
static const nir_search_variable replace354_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace354_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace354_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace354_0_0.value, &replace354_0_1.value },
   NULL,
};
static const nir_search_expression replace354 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace354_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_usub_borrow_xforms[] = {
   { &search354, &replace354.value, 25 },
};
   
static const nir_search_variable search355_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search355_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search355_2 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search355_3 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search355 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_insert,
   { &search355_0.value, &search355_1.value, &search355_2.value, &search355_3.value },
   NULL,
};
   
static const nir_search_constant replace355_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace355_0_1 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace355_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace355_0_0.value, &replace355_0_1.value },
   NULL,
};

static const nir_search_variable replace355_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace355_2_0_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace355_2_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace355_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace355_2_0_0.value, &replace355_2_0_1.value },
   NULL,
};

static const nir_search_variable replace355_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace355_2_2 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace355_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfi,
   { &replace355_2_0.value, &replace355_2_1.value, &replace355_2_2.value },
   NULL,
};
static const nir_search_expression replace355 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace355_0.value, &replace355_1.value, &replace355_2.value },
   NULL,
};
   
static const nir_search_variable search356_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search356_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search356_2 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search356_3 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search356 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_insert,
   { &search356_0.value, &search356_1.value, &search356_2.value, &search356_3.value },
   NULL,
};
   
static const nir_search_constant replace356_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace356_0_1 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace356_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace356_0_0.value, &replace356_0_1.value },
   NULL,
};

static const nir_search_variable replace356_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace356_2_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace356_2_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace356_2_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace356_2_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace356_2_0_1_0_0.value, &replace356_2_0_1_0_1.value },
   NULL,
};
static const nir_search_expression replace356_2_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace356_2_0_1_0.value },
   NULL,
};
static const nir_search_expression replace356_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace356_2_0_0.value, &replace356_2_0_1.value },
   NULL,
};

static const nir_search_variable replace356_2_1_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace356_2_1_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace356_2_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace356_2_1_0_0.value, &replace356_2_1_0_1.value },
   NULL,
};

static const nir_search_variable replace356_2_1_1_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace356_2_1_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace356_2_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace356_2_1_1_0.value, &replace356_2_1_1_1.value },
   NULL,
};
static const nir_search_expression replace356_2_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace356_2_1_0.value, &replace356_2_1_1.value },
   NULL,
};
static const nir_search_expression replace356_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace356_2_0.value, &replace356_2_1.value },
   NULL,
};
static const nir_search_expression replace356 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace356_0.value, &replace356_1.value, &replace356_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_bitfield_insert_xforms[] = {
   { &search355, &replace355.value, 26 },
   { &search356, &replace356.value, 27 },
};
   
static const nir_search_variable search357_0 = {
   { nir_search_value_variable, 0 },
   0, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search357_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search357 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &search357_0.value, &search357_1.value },
   NULL,
};
   
static const nir_search_constant replace357_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};

static const nir_search_variable replace357_0_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace357_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace357_0_0_0.value, &replace357_0_0_1.value },
   NULL,
};

static const nir_search_constant replace357_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace357_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace357_0_0.value, &replace357_0_1.value },
   NULL,
};

static const nir_search_variable replace357_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace357 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace357_0.value, &replace357_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_bfm_xforms[] = {
   { &search357, &replace357.value, 28 },
};
   
static const nir_search_variable search358_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search358_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search358_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search358 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibitfield_extract,
   { &search358_0.value, &search358_1.value, &search358_2.value },
   NULL,
};
   
static const nir_search_constant replace358_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace358_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace358_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace358_0_0.value, &replace358_0_1.value },
   NULL,
};

static const nir_search_variable replace358_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace358_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace358_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace358_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace358_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibfe,
   { &replace358_2_0.value, &replace358_2_1.value, &replace358_2_2.value },
   NULL,
};
static const nir_search_expression replace358 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace358_0.value, &replace358_1.value, &replace358_2.value },
   NULL,
};
   
static const nir_search_variable search360_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search360_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search360_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search360 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibitfield_extract,
   { &search360_0.value, &search360_1.value, &search360_2.value },
   NULL,
};
   
static const nir_search_constant replace360_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace360_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace360_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace360_0_0.value, &replace360_0_1.value },
   NULL,
};

static const nir_search_constant replace360_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace360_2_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace360_2_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x20 /* 32 */ },
};

static const nir_search_variable replace360_2_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace360_2_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace360_2_0_1_0_0.value, &replace360_2_0_1_0_1.value },
   NULL,
};

static const nir_search_variable replace360_2_0_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace360_2_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace360_2_0_1_0.value, &replace360_2_0_1_1.value },
   NULL,
};
static const nir_search_expression replace360_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace360_2_0_0.value, &replace360_2_0_1.value },
   NULL,
};

static const nir_search_constant replace360_2_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x20 /* 32 */ },
};

static const nir_search_variable replace360_2_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace360_2_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace360_2_1_0.value, &replace360_2_1_1.value },
   NULL,
};
static const nir_search_expression replace360_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace360_2_0.value, &replace360_2_1.value },
   NULL,
};
static const nir_search_expression replace360 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace360_0.value, &replace360_1.value, &replace360_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ibitfield_extract_xforms[] = {
   { &search358, &replace358.value, 29 },
   { &search360, &replace360.value, 30 },
};
   
static const nir_search_variable search359_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search359_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search359_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search359 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubitfield_extract,
   { &search359_0.value, &search359_1.value, &search359_2.value },
   NULL,
};
   
static const nir_search_constant replace359_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace359_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace359_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace359_0_0.value, &replace359_0_1.value },
   NULL,
};

static const nir_search_variable replace359_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace359_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace359_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace359_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace359_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubfe,
   { &replace359_2_0.value, &replace359_2_1.value, &replace359_2_2.value },
   NULL,
};
static const nir_search_expression replace359 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace359_0.value, &replace359_1.value, &replace359_2.value },
   NULL,
};
   
static const nir_search_variable search361_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search361_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search361_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search361 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubitfield_extract,
   { &search361_0.value, &search361_1.value, &search361_2.value },
   NULL,
};
   
static const nir_search_variable replace361_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace361_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace361_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace361_0_0.value, &replace361_0_1.value },
   NULL,
};

static const nir_search_variable replace361_1_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace361_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x20 /* 32 */ },
};
static const nir_search_expression replace361_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace361_1_0_0.value, &replace361_1_0_1.value },
   NULL,
};

static const nir_search_constant replace361_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffffffff /* 4294967295 */ },
};

static const nir_search_variable replace361_1_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace361_1_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace361_1_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace361_1_2_0.value, &replace361_1_2_1.value },
   NULL,
};
static const nir_search_expression replace361_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace361_1_0.value, &replace361_1_1.value, &replace361_1_2.value },
   NULL,
};
static const nir_search_expression replace361 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace361_0.value, &replace361_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ubitfield_extract_xforms[] = {
   { &search359, &replace359.value, 29 },
   { &search361, &replace361.value, 30 },
};
   
static const nir_search_variable search362_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search362 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ifind_msb,
   { &search362_0.value },
   NULL,
};
   
static const nir_search_variable replace362_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace362_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace362_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace362_0_0_0.value, &replace362_0_0_1.value },
   NULL,
};

static const nir_search_variable replace362_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace362_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace362_0_1_0.value },
   NULL,
};

static const nir_search_variable replace362_0_2 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace362_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace362_0_0.value, &replace362_0_1.value, &replace362_0_2.value },
   NULL,
};
static const nir_search_expression replace362 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &replace362_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ifind_msb_xforms[] = {
   { &search362, &replace362.value, 31 },
};
   
static const nir_search_variable search363_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search363 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_find_lsb,
   { &search363_0.value },
   NULL,
};
   
static const nir_search_variable replace363_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace363_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace363_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace363_0_1_0.value },
   NULL,
};
static const nir_search_expression replace363_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace363_0_0.value, &replace363_0_1.value },
   NULL,
};
static const nir_search_expression replace363 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ufind_msb,
   { &replace363_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_find_lsb_xforms[] = {
   { &search363, &replace363.value, 32 },
};
   
static const nir_search_variable search364_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search364_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search364 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &search364_0.value, &search364_1.value },
   NULL,
};
   
static const nir_search_variable replace364_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace364_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};

static const nir_search_variable replace364_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace364_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace364_0_1_0_0.value, &replace364_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace364_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression replace364_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace364_0_1_0.value, &replace364_0_1_1.value },
   NULL,
};
static const nir_search_expression replace364_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace364_0_0.value, &replace364_0_1.value },
   NULL,
};

static const nir_search_constant replace364_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression replace364 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace364_0.value, &replace364_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_i8_xforms[] = {
   { &search364, &replace364.value, 33 },
};
   
static const nir_search_variable search366_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search366_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search366 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &search366_0.value, &search366_1.value },
   NULL,
};
   
static const nir_search_variable replace366_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace366_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};

static const nir_search_variable replace366_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace366_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace366_0_1_0_0.value, &replace366_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace366_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace366_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace366_0_1_0.value, &replace366_0_1_1.value },
   NULL,
};
static const nir_search_expression replace366_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace366_0_0.value, &replace366_0_1.value },
   NULL,
};

static const nir_search_constant replace366_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace366 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace366_0.value, &replace366_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_i16_xforms[] = {
   { &search366, &replace366.value, 34 },
};
   
static const nir_search_variable search367_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search367_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search367 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &search367_0.value, &search367_1.value },
   NULL,
};
   
static const nir_search_variable replace367_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace367_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace367_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace367_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace367_0_1_0.value, &replace367_0_1_1.value },
   NULL,
};
static const nir_search_expression replace367_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace367_0_0.value, &replace367_0_1.value },
   NULL,
};

static const nir_search_constant replace367_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};
static const nir_search_expression replace367 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace367_0.value, &replace367_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_u16_xforms[] = {
   { &search367, &replace367.value, 34 },
};
   
static const nir_search_variable search368_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search368 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_2x16,
   { &search368_0.value },
   NULL,
};
   
static const nir_search_variable replace368_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace368_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace368_0_0_0_0_0.value },
   NULL,
};

static const nir_search_constant replace368_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
static const nir_search_expression replace368_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace368_0_0_0_0.value, &replace368_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace368_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace368_0_0_0.value },
   NULL,
};
static const nir_search_expression replace368_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &replace368_0_0.value },
   NULL,
};
static const nir_search_expression replace368 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace368_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_unorm_2x16_xforms[] = {
   { &search368, &replace368.value, 35 },
};
   
static const nir_search_variable search369_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search369 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_4x8,
   { &search369_0.value },
   NULL,
};
   
static const nir_search_variable replace369_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace369_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace369_0_0_0_0_0.value },
   NULL,
};

static const nir_search_constant replace369_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
static const nir_search_expression replace369_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace369_0_0_0_0.value, &replace369_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace369_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace369_0_0_0.value },
   NULL,
};
static const nir_search_expression replace369_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &replace369_0_0.value },
   NULL,
};
static const nir_search_expression replace369 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace369_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_unorm_4x8_xforms[] = {
   { &search369, &replace369.value, 36 },
};
   
static const nir_search_variable search370_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search370 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_2x16,
   { &search370_0.value },
   NULL,
};
   
static const nir_search_constant replace370_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace370_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace370_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace370_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace370_0_0_0_0_1_0.value, &replace370_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace370_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace370_0_0_0_0_0.value, &replace370_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace370_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
static const nir_search_expression replace370_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace370_0_0_0_0.value, &replace370_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace370_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace370_0_0_0.value },
   NULL,
};
static const nir_search_expression replace370_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &replace370_0_0.value },
   NULL,
};
static const nir_search_expression replace370 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace370_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_snorm_2x16_xforms[] = {
   { &search370, &replace370.value, 37 },
};
   
static const nir_search_variable search371_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search371 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_4x8,
   { &search371_0.value },
   NULL,
};
   
static const nir_search_constant replace371_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace371_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace371_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace371_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace371_0_0_0_0_1_0.value, &replace371_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace371_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace371_0_0_0_0_0.value, &replace371_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace371_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
static const nir_search_expression replace371_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace371_0_0_0_0.value, &replace371_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace371_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace371_0_0_0.value },
   NULL,
};
static const nir_search_expression replace371_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &replace371_0_0.value },
   NULL,
};
static const nir_search_expression replace371 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace371_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_snorm_4x8_xforms[] = {
   { &search371, &replace371.value, 38 },
};
   
static const nir_search_variable search372_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search372 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_2x16,
   { &search372_0.value },
   NULL,
};
   
static const nir_search_variable replace372_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace372_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace372_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace372_0_0_0_0.value, &replace372_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace372_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace372_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace372_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace372_0_0_1_0.value, &replace372_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace372_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace372_0_0_0.value, &replace372_0_0_1.value },
   NULL,
};
static const nir_search_expression replace372_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f32,
   { &replace372_0_0.value },
   NULL,
};

static const nir_search_constant replace372_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
static const nir_search_expression replace372 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace372_0.value, &replace372_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_unorm_2x16_xforms[] = {
   { &search372, &replace372.value, 39 },
};
   
static const nir_search_variable search373_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search373 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_4x8,
   { &search373_0.value },
   NULL,
};
   
static const nir_search_variable replace373_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace373_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace373_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace373_0_0_0_0.value, &replace373_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace373_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace373_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace373_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace373_0_0_1_0.value, &replace373_0_0_1_1.value },
   NULL,
};

static const nir_search_variable replace373_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace373_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace373_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace373_0_0_2_0.value, &replace373_0_0_2_1.value },
   NULL,
};

static const nir_search_variable replace373_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace373_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace373_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace373_0_0_3_0.value, &replace373_0_0_3_1.value },
   NULL,
};
static const nir_search_expression replace373_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace373_0_0_0.value, &replace373_0_0_1.value, &replace373_0_0_2.value, &replace373_0_0_3.value },
   NULL,
};
static const nir_search_expression replace373_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f32,
   { &replace373_0_0.value },
   NULL,
};

static const nir_search_constant replace373_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
static const nir_search_expression replace373 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace373_0.value, &replace373_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_unorm_4x8_xforms[] = {
   { &search373, &replace373.value, 40 },
};
   
static const nir_search_variable search374_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search374 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_2x16,
   { &search374_0.value },
   NULL,
};
   
static const nir_search_constant replace374_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace374_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace374_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace374_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace374_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace374_1_1_0_0_0_0.value, &replace374_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace374_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace374_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace374_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace374_1_1_0_0_1_0.value, &replace374_1_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace374_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace374_1_1_0_0_0.value, &replace374_1_1_0_0_1.value },
   NULL,
};
static const nir_search_expression replace374_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f32,
   { &replace374_1_1_0_0.value },
   NULL,
};

static const nir_search_constant replace374_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
static const nir_search_expression replace374_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace374_1_1_0.value, &replace374_1_1_1.value },
   NULL,
};
static const nir_search_expression replace374_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace374_1_0.value, &replace374_1_1.value },
   NULL,
};
static const nir_search_expression replace374 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace374_0.value, &replace374_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_snorm_2x16_xforms[] = {
   { &search374, &replace374.value, 41 },
};
   
static const nir_search_variable search375_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search375 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_4x8,
   { &search375_0.value },
   NULL,
};
   
static const nir_search_constant replace375_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace375_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace375_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace375_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace375_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace375_1_1_0_0_0_0.value, &replace375_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace375_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace375_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace375_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace375_1_1_0_0_1_0.value, &replace375_1_1_0_0_1_1.value },
   NULL,
};

static const nir_search_variable replace375_1_1_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace375_1_1_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace375_1_1_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace375_1_1_0_0_2_0.value, &replace375_1_1_0_0_2_1.value },
   NULL,
};

static const nir_search_variable replace375_1_1_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace375_1_1_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace375_1_1_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace375_1_1_0_0_3_0.value, &replace375_1_1_0_0_3_1.value },
   NULL,
};
static const nir_search_expression replace375_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace375_1_1_0_0_0.value, &replace375_1_1_0_0_1.value, &replace375_1_1_0_0_2.value, &replace375_1_1_0_0_3.value },
   NULL,
};
static const nir_search_expression replace375_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f32,
   { &replace375_1_1_0_0.value },
   NULL,
};

static const nir_search_constant replace375_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
static const nir_search_expression replace375_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace375_1_1_0.value, &replace375_1_1_1.value },
   NULL,
};
static const nir_search_expression replace375_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace375_1_0.value, &replace375_1_1.value },
   NULL,
};
static const nir_search_expression replace375 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace375_0.value, &replace375_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_snorm_4x8_xforms[] = {
   { &search375, &replace375.value, 42 },
};
   
static const nir_search_variable search396_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search396_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search396 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ldexp,
   { &search396_0.value, &search396_1.value },
   NULL,
};
   
static const nir_search_variable replace396_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace396_0_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace396_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace396_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace396_0_1_0_0_0_0_0.value, &replace396_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace396_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace396_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace396_0_1_0_0_0_0.value, &replace396_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace396_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace396_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace396_0_1_0_0_0.value, &replace396_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace396_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
static const nir_search_expression replace396_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace396_0_1_0_0.value, &replace396_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace396_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
static const nir_search_expression replace396_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace396_0_1_0.value, &replace396_0_1_1.value },
   NULL,
};
static const nir_search_expression replace396_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace396_0_0.value, &replace396_0_1.value },
   NULL,
};

static const nir_search_variable replace396_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace396_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace396_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace396_1_0_0_0_0_0.value, &replace396_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace396_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace396_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace396_1_0_0_0_0.value, &replace396_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace396_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace396_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace396_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace396_1_0_0_1_0_0_0.value, &replace396_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace396_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace396_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace396_1_0_0_1_0_0.value, &replace396_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace396_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace396_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace396_1_0_0_1_0.value, &replace396_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace396_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace396_1_0_0_0.value, &replace396_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace396_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
static const nir_search_expression replace396_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace396_1_0_0.value, &replace396_1_0_1.value },
   NULL,
};

static const nir_search_constant replace396_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
static const nir_search_expression replace396_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace396_1_0.value, &replace396_1_1.value },
   NULL,
};
static const nir_search_expression replace396 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace396_0.value, &replace396_1.value },
   NULL,
};
   
static const nir_search_variable search397_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search397_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search397 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_ldexp,
   { &search397_0.value, &search397_1.value },
   NULL,
};
   
static const nir_search_variable replace397_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace397_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace397_0_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace397_0_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace397_0_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace397_0_1_1_0_0_0_0_0.value, &replace397_0_1_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace397_0_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace397_0_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace397_0_1_1_0_0_0_0.value, &replace397_0_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace397_0_1_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace397_0_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace397_0_1_1_0_0_0.value, &replace397_0_1_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace397_0_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
static const nir_search_expression replace397_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace397_0_1_1_0_0.value, &replace397_0_1_1_0_1.value },
   NULL,
};

static const nir_search_constant replace397_0_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
static const nir_search_expression replace397_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace397_0_1_1_0.value, &replace397_0_1_1_1.value },
   NULL,
};
static const nir_search_expression replace397_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &replace397_0_1_0.value, &replace397_0_1_1.value },
   NULL,
};
static const nir_search_expression replace397_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace397_0_0.value, &replace397_0_1.value },
   NULL,
};

static const nir_search_constant replace397_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace397_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace397_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace397_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace397_1_1_0_0_0_0_0.value, &replace397_1_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace397_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace397_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace397_1_1_0_0_0_0.value, &replace397_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace397_1_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace397_1_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace397_1_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace397_1_1_0_0_1_0_0_0.value, &replace397_1_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace397_1_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace397_1_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace397_1_1_0_0_1_0_0.value, &replace397_1_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace397_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace397_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace397_1_1_0_0_1_0.value, &replace397_1_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace397_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace397_1_1_0_0_0.value, &replace397_1_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace397_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
static const nir_search_expression replace397_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace397_1_1_0_0.value, &replace397_1_1_0_1.value },
   NULL,
};

static const nir_search_constant replace397_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
static const nir_search_expression replace397_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace397_1_1_0.value, &replace397_1_1_1.value },
   NULL,
};
static const nir_search_expression replace397_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &replace397_1_0.value, &replace397_1_1.value },
   NULL,
};
static const nir_search_expression replace397 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace397_0.value, &replace397_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ldexp_xforms[] = {
   { &search396, &replace396.value, 43 },
   { &search397, &replace397.value, 43 },
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
      case nir_op_fdot4:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fdot4_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fdot4_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fdot3:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fdot3_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fdot3_xforms[i];
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
      case nir_op_f2i32:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_f2i32_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_f2i32_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_f2u32:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_f2u32_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_f2u32_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_unpack_64_2x32_split_x:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_unpack_64_2x32_split_x_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_unpack_64_2x32_split_x_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_unpack_64_2x32_split_y:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_unpack_64_2x32_split_y_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_unpack_64_2x32_split_y_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_pack_64_2x32_split:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_pack_64_2x32_split_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_pack_64_2x32_split_xforms[i];
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
      case nir_op_bfm:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_bfm_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_bfm_xforms[i];
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
      case nir_op_ifind_msb:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ifind_msb_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ifind_msb_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_find_lsb:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_find_lsb_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_find_lsb_xforms[i];
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
   bool condition_flags[44];
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
   condition_flags[12] = !options->lower_b2f;
   condition_flags[13] = options->lower_fpow;
   condition_flags[14] = !options->lower_fpow;
   condition_flags[15] = options->lower_fdiv;
   condition_flags[16] = options->lower_fsqrt;
   condition_flags[17] = !options->lower_fsqrt;
   condition_flags[18] = !options->lower_extract_byte;
   condition_flags[19] = !options->lower_extract_word;
   condition_flags[20] = options->lower_sub;
   condition_flags[21] = options->lower_negate;
   condition_flags[22] = options->lower_fmod32;
   condition_flags[23] = options->lower_fmod64;
   condition_flags[24] = options->lower_uadd_carry;
   condition_flags[25] = options->lower_usub_borrow;
   condition_flags[26] = options->lower_bitfield_insert;
   condition_flags[27] = options->lower_bitfield_insert_to_shifts;
   condition_flags[28] = options->lower_bfm;
   condition_flags[29] = options->lower_bitfield_extract;
   condition_flags[30] = options->lower_bitfield_extract_to_shifts;
   condition_flags[31] = options->lower_ifind_msb;
   condition_flags[32] = options->lower_find_lsb;
   condition_flags[33] = options->lower_extract_byte;
   condition_flags[34] = options->lower_extract_word;
   condition_flags[35] = options->lower_pack_unorm_2x16;
   condition_flags[36] = options->lower_pack_unorm_4x8;
   condition_flags[37] = options->lower_pack_snorm_2x16;
   condition_flags[38] = options->lower_pack_snorm_4x8;
   condition_flags[39] = options->lower_unpack_unorm_2x16;
   condition_flags[40] = options->lower_unpack_unorm_4x8;
   condition_flags[41] = options->lower_unpack_snorm_2x16;
   condition_flags[42] = options->lower_unpack_snorm_4x8;
   condition_flags[43] = options->lower_ldexp;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_algebraic_impl(function->impl, condition_flags);
   }

   return progress;
}


#include "nir.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

   
static const nir_search_variable search429_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search429_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search429_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search429_0_0.value, &search429_0_1.value },
   (is_used_once),
};

static const nir_search_variable search429_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search429 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search429_0.value, &search429_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace429_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace429_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace429_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace429_0_0.value, &replace429_0_1.value },
   NULL,
};

static const nir_search_variable replace429_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace429 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace429_0.value, &replace429_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_before_ffma_fmul_xforms[] = {
   { &search429, &replace429.value, 0 },
};
   
static const nir_search_variable search430_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search430_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search430_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search430_0_0.value, &search430_0_1.value },
   (is_used_once),
};

static const nir_search_variable search430_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search430 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search430_0.value, &search430_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace430_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace430_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace430_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace430_0_0.value, &replace430_0_1.value },
   NULL,
};

static const nir_search_variable replace430_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace430 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace430_0.value, &replace430_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_before_ffma_imul_xforms[] = {
   { &search430, &replace430.value, 0 },
};
   
static const nir_search_variable search431_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search431_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search431_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search431_0_0.value, &search431_0_1.value },
   (is_used_once),
};

static const nir_search_variable search431_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search431 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search431_0.value, &search431_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace431_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace431_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace431_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace431_0_0.value, &replace431_0_1.value },
   NULL,
};

static const nir_search_variable replace431_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace431 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace431_0.value, &replace431_1.value },
   NULL,
};
   
static const nir_search_variable search433_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search433_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search433_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search433_0_0.value, &search433_0_1.value },
   NULL,
};

static const nir_search_variable search433_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search433_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search433_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search433_1_0.value, &search433_1_1.value },
   NULL,
};
static const nir_search_expression search433 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search433_0.value, &search433_1.value },
   NULL,
};
   
static const nir_search_variable replace433_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace433_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace433_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace433_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace433_1_0.value, &replace433_1_1.value },
   NULL,
};
static const nir_search_expression replace433 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace433_0.value, &replace433_1.value },
   NULL,
};
   
static const nir_search_variable search435_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search435_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search435_0_0.value },
   NULL,
};

static const nir_search_variable search435_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search435 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search435_0.value, &search435_1.value },
   NULL,
};
   
static const nir_search_constant replace435 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search439_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search439_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search439_0_0.value },
   NULL,
};

static const nir_search_variable search439_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search439_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search439_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search439_1_0.value, &search439_1_1.value },
   NULL,
};
static const nir_search_expression search439 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search439_0.value, &search439_1.value },
   NULL,
};
   
static const nir_search_variable replace439 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search440_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search440_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search440_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search440_1_0_0.value },
   NULL,
};

static const nir_search_variable search440_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search440_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search440_1_0.value, &search440_1_1.value },
   NULL,
};
static const nir_search_expression search440 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search440_0.value, &search440_1.value },
   NULL,
};
   
static const nir_search_variable replace440 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_before_ffma_fadd_xforms[] = {
   { &search431, &replace431.value, 0 },
   { &search433, &replace433.value, 0 },
   { &search435, &replace435.value, 0 },
   { &search439, &replace439.value, 0 },
   { &search440, &replace440.value, 0 },
};
   
static const nir_search_variable search432_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   (is_not_const),
};

static const nir_search_variable search432_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search432_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search432_0_0.value, &search432_0_1.value },
   (is_used_once),
};

static const nir_search_variable search432_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   (is_not_const),
};
static const nir_search_expression search432 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search432_0.value, &search432_1.value },
   (is_used_once),
};
   
static const nir_search_variable replace432_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace432_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace432_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace432_0_0.value, &replace432_0_1.value },
   NULL,
};

static const nir_search_variable replace432_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace432 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace432_0.value, &replace432_1.value },
   NULL,
};
   
static const nir_search_variable search434_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search434_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search434_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search434_0_0.value, &search434_0_1.value },
   NULL,
};

static const nir_search_variable search434_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search434_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search434_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search434_1_0.value, &search434_1_1.value },
   NULL,
};
static const nir_search_expression search434 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search434_0.value, &search434_1.value },
   NULL,
};
   
static const nir_search_variable replace434_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace434_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace434_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace434_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace434_1_0.value, &replace434_1_1.value },
   NULL,
};
static const nir_search_expression replace434 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace434_0.value, &replace434_1.value },
   NULL,
};
   
static const nir_search_variable search436_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search436_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search436_0_0.value },
   NULL,
};

static const nir_search_variable search436_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search436 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search436_0.value, &search436_1.value },
   NULL,
};
   
static const nir_search_constant replace436 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search437_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search437_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search437_0_0.value },
   NULL,
};

static const nir_search_variable search437_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search437_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search437_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search437_1_0.value, &search437_1_1.value },
   NULL,
};
static const nir_search_expression search437 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search437_0.value, &search437_1.value },
   NULL,
};
   
static const nir_search_variable replace437 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search438_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search438_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search438_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search438_1_0_0.value },
   NULL,
};

static const nir_search_variable search438_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search438_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search438_1_0.value, &search438_1_1.value },
   NULL,
};
static const nir_search_expression search438 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search438_0.value, &search438_1.value },
   NULL,
};
   
static const nir_search_variable replace438 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_before_ffma_iadd_xforms[] = {
   { &search432, &replace432.value, 0 },
   { &search434, &replace434.value, 0 },
   { &search436, &replace436.value, 0 },
   { &search437, &replace437.value, 0 },
   { &search438, &replace438.value, 0 },
};

static bool
nir_opt_algebraic_before_ffma_block(nir_block *block, const bool *condition_flags,
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
      case nir_op_fmul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_before_ffma_fmul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_before_ffma_fmul_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_imul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_before_ffma_imul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_before_ffma_imul_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_fadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_before_ffma_fadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_before_ffma_fadd_xforms[i];
            if (condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  mem_ctx)) {
               progress = true;
               break;
            }
         }
         break;
      case nir_op_iadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_before_ffma_iadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_before_ffma_iadd_xforms[i];
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
nir_opt_algebraic_before_ffma_impl(nir_function_impl *impl, const bool *condition_flags)
{
   void *mem_ctx = ralloc_parent(impl);
   bool progress = false;

   nir_foreach_block_reverse(block, impl) {
      progress |= nir_opt_algebraic_before_ffma_block(block, condition_flags, mem_ctx);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}


bool
nir_opt_algebraic_before_ffma(nir_shader *shader)
{
   bool progress = false;
   bool condition_flags[44];
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
   condition_flags[12] = !options->lower_b2f;
   condition_flags[13] = options->lower_fpow;
   condition_flags[14] = !options->lower_fpow;
   condition_flags[15] = options->lower_fdiv;
   condition_flags[16] = options->lower_fsqrt;
   condition_flags[17] = !options->lower_fsqrt;
   condition_flags[18] = !options->lower_extract_byte;
   condition_flags[19] = !options->lower_extract_word;
   condition_flags[20] = options->lower_sub;
   condition_flags[21] = options->lower_negate;
   condition_flags[22] = options->lower_fmod32;
   condition_flags[23] = options->lower_fmod64;
   condition_flags[24] = options->lower_uadd_carry;
   condition_flags[25] = options->lower_usub_borrow;
   condition_flags[26] = options->lower_bitfield_insert;
   condition_flags[27] = options->lower_bitfield_insert_to_shifts;
   condition_flags[28] = options->lower_bfm;
   condition_flags[29] = options->lower_bitfield_extract;
   condition_flags[30] = options->lower_bitfield_extract_to_shifts;
   condition_flags[31] = options->lower_ifind_msb;
   condition_flags[32] = options->lower_find_lsb;
   condition_flags[33] = options->lower_extract_byte;
   condition_flags[34] = options->lower_extract_word;
   condition_flags[35] = options->lower_pack_unorm_2x16;
   condition_flags[36] = options->lower_pack_unorm_4x8;
   condition_flags[37] = options->lower_pack_snorm_2x16;
   condition_flags[38] = options->lower_pack_snorm_4x8;
   condition_flags[39] = options->lower_unpack_unorm_2x16;
   condition_flags[40] = options->lower_unpack_unorm_4x8;
   condition_flags[41] = options->lower_unpack_snorm_2x16;
   condition_flags[42] = options->lower_unpack_snorm_4x8;
   condition_flags[43] = options->lower_ldexp;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_algebraic_before_ffma_impl(function->impl, condition_flags);
   }

   return progress;
}


#include "nir.h"
#include "nir_search.h"
#include "nir_search_helpers.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

   
static const nir_search_variable search441_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search441_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search441_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search441_0_0.value, &search441_0_1.value },
   NULL,
};

static const nir_search_constant search441_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search441 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search441_0.value, &search441_1.value },
   NULL,
};
   
static const nir_search_variable replace441_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace441_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace441_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace441_1_0.value },
   NULL,
};
static const nir_search_expression replace441 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace441_0.value, &replace441_1.value },
   NULL,
};
   
static const nir_search_variable search442_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search442_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search442_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search442_0_0_0.value, &search442_0_0_1.value },
   NULL,
};
static const nir_search_expression search442_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search442_0_0.value },
   NULL,
};

static const nir_search_constant search442_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search442 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search442_0.value, &search442_1.value },
   NULL,
};
   
static const nir_search_variable replace442_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace442_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace442_0_0.value },
   NULL,
};

static const nir_search_variable replace442_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace442 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace442_0.value, &replace442_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_flt_xforms[] = {
   { &search441, &replace441.value, 0 },
   { &search442, &replace442.value, 0 },
};
   
static const nir_search_variable search443_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search443_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search443_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search443_0_0.value, &search443_0_1.value },
   NULL,
};

static const nir_search_constant search443_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search443 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search443_0.value, &search443_1.value },
   NULL,
};
   
static const nir_search_variable replace443_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace443_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace443_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace443_1_0.value },
   NULL,
};
static const nir_search_expression replace443 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace443_0.value, &replace443_1.value },
   NULL,
};
   
static const nir_search_variable search444_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search444_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search444_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search444_0_0_0.value, &search444_0_0_1.value },
   NULL,
};
static const nir_search_expression search444_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search444_0_0.value },
   NULL,
};

static const nir_search_constant search444_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search444 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search444_0.value, &search444_1.value },
   NULL,
};
   
static const nir_search_variable replace444_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace444_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace444_0_0.value },
   NULL,
};

static const nir_search_variable replace444_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace444 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace444_0.value, &replace444_1.value },
   NULL,
};
   
static const nir_search_variable search447_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search447_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search447_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search447_0_0_0.value, &search447_0_0_1.value },
   (is_used_once),
};

static const nir_search_variable search447_0_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search447_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search447_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search447_0_1_0.value, &search447_0_1_1.value },
   NULL,
};
static const nir_search_expression search447_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search447_0_0.value, &search447_0_1.value },
   (is_used_once),
};

static const nir_search_constant search447_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search447 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search447_0.value, &search447_1.value },
   NULL,
};
   
static const nir_search_variable replace447_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace447_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace447_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace447_0_1_0.value },
   NULL,
};
static const nir_search_expression replace447_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace447_0_0.value, &replace447_0_1.value },
   NULL,
};

static const nir_search_variable replace447_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace447_1_1_0 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace447_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace447_1_1_0.value },
   NULL,
};
static const nir_search_expression replace447_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace447_1_0.value, &replace447_1_1.value },
   NULL,
};
static const nir_search_expression replace447 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace447_0.value, &replace447_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fge_xforms[] = {
   { &search443, &replace443.value, 0 },
   { &search444, &replace444.value, 0 },
   { &search447, &replace447.value, 0 },
};
   
static const nir_search_variable search445_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search445_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search445_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search445_0_0.value, &search445_0_1.value },
   NULL,
};

static const nir_search_constant search445_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search445 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_feq,
   { &search445_0.value, &search445_1.value },
   NULL,
};
   
static const nir_search_variable replace445_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace445_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace445_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace445_1_0.value },
   NULL,
};
static const nir_search_expression replace445 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace445_0.value, &replace445_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_feq_xforms[] = {
   { &search445, &replace445.value, 0 },
};
   
static const nir_search_variable search446_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search446_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search446_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search446_0_0.value, &search446_0_1.value },
   NULL,
};

static const nir_search_constant search446_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search446 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fne,
   { &search446_0.value, &search446_1.value },
   NULL,
};
   
static const nir_search_variable replace446_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace446_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace446_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace446_1_0.value },
   NULL,
};
static const nir_search_expression replace446 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace446_0.value, &replace446_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fne_xforms[] = {
   { &search446, &replace446.value, 0 },
};
   
static const nir_search_variable search448_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search448_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search448 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot2,
   { &search448_0.value, &search448_1.value },
   NULL,
};
   
static const nir_search_variable replace448_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace448_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace448 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated2,
   { &replace448_0.value, &replace448_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot2_xforms[] = {
   { &search448, &replace448.value, 44 },
};
   
static const nir_search_variable search449_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search449_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search449 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search449_0.value, &search449_1.value },
   NULL,
};
   
static const nir_search_variable replace449_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace449_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace449 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated3,
   { &replace449_0.value, &replace449_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot3_xforms[] = {
   { &search449, &replace449.value, 44 },
};
   
static const nir_search_variable search450_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search450_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search450 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search450_0.value, &search450_1.value },
   NULL,
};
   
static const nir_search_variable replace450_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace450_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace450 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated4,
   { &replace450_0.value, &replace450_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot4_xforms[] = {
   { &search450, &replace450.value, 44 },
};
   
static const nir_search_variable search451_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search451_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search451 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph,
   { &search451_0.value, &search451_1.value },
   NULL,
};
   
static const nir_search_variable replace451_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace451_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace451 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph_replicated,
   { &replace451_0.value, &replace451_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdph_xforms[] = {
   { &search451, &replace451.value, 44 },
};
   
static const nir_search_variable search452_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search452_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search452_0_0.value },
   NULL,
};
static const nir_search_expression search452 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search452_0.value },
   (is_used_more_than_once),
};
   
static const nir_search_variable replace452_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace452_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant replace452_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace452 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace452_0.value, &replace452_1.value, &replace452_2.value },
   NULL,
};
   
static const nir_search_variable search456_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search456 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_b2f,
   { &search456_0.value },
   NULL,
};
   
static const nir_search_variable replace456_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace456_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace456 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace456_0.value, &replace456_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_b2f_xforms[] = {
   { &search452, &replace452.value, 0 },
   { &search456, &replace456.value, 45 },
};
   
static const nir_search_variable search453_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search453_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search453_0_0_0.value },
   NULL,
};
static const nir_search_expression search453_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search453_0_0.value },
   NULL,
};
static const nir_search_expression search453 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search453_0.value },
   (is_used_more_than_once),
};
   
static const nir_search_variable replace453_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace453_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};

static const nir_search_constant replace453_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
static const nir_search_expression replace453 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace453_0.value, &replace453_1.value, &replace453_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fneg_xforms[] = {
   { &search453, &replace453.value, 0 },
};
   
static const nir_search_variable search454_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search454_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search454_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search454_0_0.value, &search454_0_1.value },
   (is_used_once),
};

static const nir_search_variable search454_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search454_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search454_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search454_1_0.value, &search454_1_1.value },
   (is_used_once),
};
static const nir_search_expression search454 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search454_0.value, &search454_1.value },
   NULL,
};
   
static const nir_search_variable replace454_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace454_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace454_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace454_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace454_1_0.value, &replace454_1_1.value },
   NULL,
};
static const nir_search_expression replace454 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace454_0.value, &replace454_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fmin_xforms[] = {
   { &search454, &replace454.value, 0 },
};
   
static const nir_search_variable search455_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search455_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search455_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search455_0_0.value, &search455_0_1.value },
   (is_used_once),
};

static const nir_search_variable search455_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search455_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search455_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search455_1_0.value, &search455_1_1.value },
   (is_used_once),
};
static const nir_search_expression search455 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search455_0.value, &search455_1.value },
   NULL,
};
   
static const nir_search_variable replace455_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace455_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace455_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace455_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace455_1_0.value, &replace455_1_1.value },
   NULL,
};
static const nir_search_expression replace455 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace455_0.value, &replace455_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fmax_xforms[] = {
   { &search455, &replace455.value, 0 },
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
   bool condition_flags[46];
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
   condition_flags[12] = !options->lower_b2f;
   condition_flags[13] = options->lower_fpow;
   condition_flags[14] = !options->lower_fpow;
   condition_flags[15] = options->lower_fdiv;
   condition_flags[16] = options->lower_fsqrt;
   condition_flags[17] = !options->lower_fsqrt;
   condition_flags[18] = !options->lower_extract_byte;
   condition_flags[19] = !options->lower_extract_word;
   condition_flags[20] = options->lower_sub;
   condition_flags[21] = options->lower_negate;
   condition_flags[22] = options->lower_fmod32;
   condition_flags[23] = options->lower_fmod64;
   condition_flags[24] = options->lower_uadd_carry;
   condition_flags[25] = options->lower_usub_borrow;
   condition_flags[26] = options->lower_bitfield_insert;
   condition_flags[27] = options->lower_bitfield_insert_to_shifts;
   condition_flags[28] = options->lower_bfm;
   condition_flags[29] = options->lower_bitfield_extract;
   condition_flags[30] = options->lower_bitfield_extract_to_shifts;
   condition_flags[31] = options->lower_ifind_msb;
   condition_flags[32] = options->lower_find_lsb;
   condition_flags[33] = options->lower_extract_byte;
   condition_flags[34] = options->lower_extract_word;
   condition_flags[35] = options->lower_pack_unorm_2x16;
   condition_flags[36] = options->lower_pack_unorm_4x8;
   condition_flags[37] = options->lower_pack_snorm_2x16;
   condition_flags[38] = options->lower_pack_snorm_4x8;
   condition_flags[39] = options->lower_unpack_unorm_2x16;
   condition_flags[40] = options->lower_unpack_unorm_4x8;
   condition_flags[41] = options->lower_unpack_snorm_2x16;
   condition_flags[42] = options->lower_unpack_snorm_4x8;
   condition_flags[43] = options->lower_ldexp;
   condition_flags[44] = options->fdot_replicates;
   condition_flags[45] = options->lower_b2f;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_algebraic_late_impl(function->impl, condition_flags);
   }

   return progress;
}

