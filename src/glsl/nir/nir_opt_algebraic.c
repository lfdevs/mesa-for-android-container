
#include "nir.h"
#include "nir_search.h"

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
};

   
static const nir_search_variable search35_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search35_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search35 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search35_0.value, &search35_1.value },
};
   
static const nir_search_variable replace35 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search36_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search36_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search36 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search36_0.value, &search36_1.value },
};
   
static const nir_search_constant replace36 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search42_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search42_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search42_0_0.value },
};

static const nir_search_variable search42_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search42_1 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search42_1_0.value },
};
static const nir_search_expression search42 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search42_0.value, &search42_1.value },
};
   
static const nir_search_variable replace42_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace42_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace42_0 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &replace42_0_0.value, &replace42_0_1.value },
};
static const nir_search_expression replace42 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &replace42_0.value },
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search35, &replace35.value },
   { &search36, &replace36.value },
   { &search42, &replace42.value },
};
   
static const nir_search_variable search51_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search51_0 = {
   { nir_search_value_expression },
   nir_op_flog,
   { &search51_0_0.value },
};
static const nir_search_expression search51 = {
   { nir_search_value_expression },
   nir_op_fexp,
   { &search51_0.value },
};
   
static const nir_search_variable replace51 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search55_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search55_0_0 = {
   { nir_search_value_expression },
   nir_op_flog,
   { &search55_0_0_0.value },
};

static const nir_search_variable search55_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search55_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search55_0_0.value, &search55_0_1.value },
};
static const nir_search_expression search55 = {
   { nir_search_value_expression },
   nir_op_fexp,
   { &search55_0.value },
};
   
static const nir_search_variable replace55_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace55_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace55 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &replace55_0.value, &replace55_1.value },
};

static const struct transform nir_opt_algebraic_fexp_xforms[] = {
   { &search51, &replace51.value },
   { &search55, &replace55.value },
};
   
static const nir_search_variable search40_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search40_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search40 = {
   { nir_search_value_expression },
   nir_op_ixor,
   { &search40_0.value, &search40_1.value },
};
   
static const nir_search_constant replace40 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search40, &replace40.value },
};
   
static const nir_search_variable search79_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search79_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search79_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search79_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search79_0_0.value, &search79_0_1.value, &search79_0_2.value },
};

static const nir_search_variable search79_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search79 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search79_0.value, &search79_1.value },
};
   
static const nir_search_variable replace79_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace79_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace79_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace79_1 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace79_1_0.value, &replace79_1_1.value },
};

static const nir_search_variable replace79_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace79_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace79_2 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace79_2_0.value, &replace79_2_1.value },
};
static const nir_search_expression replace79 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace79_0.value, &replace79_1.value, &replace79_2.value },
};
   
static const nir_search_variable search80_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search80_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search80_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search80_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search80_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search80_1_0.value, &search80_1_1.value, &search80_1_2.value },
};
static const nir_search_expression search80 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search80_0.value, &search80_1.value },
};
   
static const nir_search_variable replace80_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace80_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace80_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace80_1 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace80_1_0.value, &replace80_1_1.value },
};

static const nir_search_variable replace80_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace80_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace80_2 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace80_2_0.value, &replace80_2_1.value },
};
static const nir_search_expression replace80 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace80_0.value, &replace80_1.value, &replace80_2.value },
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search79, &replace79.value },
   { &search80, &replace80.value },
};
   
static const nir_search_variable search9_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search9_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search9 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search9_0.value, &search9_1.value },
};
   
static const nir_search_constant replace9 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search11_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search11_1 = {
   { nir_search_value_constant },
   { 0x1 /* 1 */ },
};
static const nir_search_expression search11 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search11_0.value, &search11_1.value },
};
   
static const nir_search_variable replace11 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search13_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search13_1 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};
static const nir_search_expression search13 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search13_0.value, &search13_1.value },
};
   
static const nir_search_variable replace13_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace13 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &replace13_0.value },
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search9, &replace9.value },
   { &search11, &replace11.value },
   { &search13, &replace13.value },
};
   
static const nir_search_variable search89_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search89_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search89_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search89_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search89_0_0.value, &search89_0_1.value, &search89_0_2.value },
};

static const nir_search_variable search89_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search89 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &search89_0.value, &search89_1.value },
};
   
static const nir_search_variable replace89_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace89_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace89_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace89_1 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace89_1_0.value, &replace89_1_1.value },
};

static const nir_search_variable replace89_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace89_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace89_2 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace89_2_0.value, &replace89_2_1.value },
};
static const nir_search_expression replace89 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace89_0.value, &replace89_1.value, &replace89_2.value },
};
   
static const nir_search_variable search90_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search90_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search90_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search90_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search90_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search90_1_0.value, &search90_1_1.value, &search90_1_2.value },
};
static const nir_search_expression search90 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &search90_0.value, &search90_1.value },
};
   
static const nir_search_variable replace90_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace90_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace90_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace90_1 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace90_1_0.value, &replace90_1_1.value },
};

static const nir_search_variable replace90_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace90_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace90_2 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace90_2_0.value, &replace90_2_1.value },
};
static const nir_search_expression replace90 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace90_0.value, &replace90_1.value, &replace90_2.value },
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search89, &replace89.value },
   { &search90, &replace90.value },
};
   
static const nir_search_variable search8_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search8_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search8 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search8_0.value, &search8_1.value },
};
   
static const nir_search_constant replace8 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search10_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search10_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search10 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search10_0.value, &search10_1.value },
};
   
static const nir_search_variable replace10 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search12_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search12_1 = {
   { nir_search_value_constant },
   { 0xbf800000 /* -1.0 */ },
};
static const nir_search_expression search12 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search12_0.value, &search12_1.value },
};
   
static const nir_search_variable replace12_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace12 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace12_0.value },
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search8, &replace8.value },
   { &search10, &replace10.value },
   { &search12, &replace12.value },
};
   
static const nir_search_constant search14_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search14_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search14_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search14 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search14_0.value, &search14_1.value, &search14_2.value },
};
   
static const nir_search_variable replace14 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search15_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search15_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search15_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search15 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search15_0.value, &search15_1.value, &search15_2.value },
};
   
static const nir_search_variable replace15 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search16_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search16_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search16_2 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search16 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search16_0.value, &search16_1.value, &search16_2.value },
};
   
static const nir_search_variable replace16_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace16_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace16 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace16_0.value, &replace16_1.value },
};
   
static const nir_search_variable search17_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search17_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search17_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search17 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search17_0.value, &search17_1.value, &search17_2.value },
};
   
static const nir_search_variable replace17_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace17_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace17 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace17_0.value, &replace17_1.value },
};
   
static const nir_search_constant search18_0 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search18_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search18_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search18 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search18_0.value, &search18_1.value, &search18_2.value },
};
   
static const nir_search_variable replace18_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace18_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace18 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace18_0.value, &replace18_1.value },
};

static const struct transform nir_opt_algebraic_ffma_xforms[] = {
   { &search14, &replace14.value },
   { &search15, &replace15.value },
   { &search16, &replace16.value },
   { &search17, &replace17.value },
   { &search18, &replace18.value },
};
   
static const nir_search_variable search63_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_bool,
};

static const nir_search_constant search63_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search63 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search63_0.value, &search63_1.value },
};
   
static const nir_search_variable replace63 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search85_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search85_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search85_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search85_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search85_0_0.value, &search85_0_1.value, &search85_0_2.value },
};

static const nir_search_variable search85_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search85 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search85_0.value, &search85_1.value },
};
   
static const nir_search_variable replace85_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace85_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace85_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace85_1 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace85_1_0.value, &replace85_1_1.value },
};

static const nir_search_variable replace85_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace85_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace85_2 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace85_2_0.value, &replace85_2_1.value },
};
static const nir_search_expression replace85 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace85_0.value, &replace85_1.value, &replace85_2.value },
};
   
static const nir_search_variable search86_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search86_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search86_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search86_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search86_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search86_1_0.value, &search86_1_1.value, &search86_1_2.value },
};
static const nir_search_expression search86 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search86_0.value, &search86_1.value },
};
   
static const nir_search_variable replace86_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace86_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace86_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace86_1 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace86_1_0.value, &replace86_1_1.value },
};

static const nir_search_variable replace86_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace86_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace86_2 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace86_2_0.value, &replace86_2_1.value },
};
static const nir_search_expression replace86 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace86_0.value, &replace86_1.value, &replace86_2.value },
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search63, &replace63.value },
   { &search85, &replace85.value },
   { &search86, &replace86.value },
};
   
static const nir_search_variable search65_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search65_1 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};

static const nir_search_constant search65_2 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};
static const nir_search_expression search65 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search65_0.value, &search65_1.value, &search65_2.value },
};
   
static const nir_search_variable replace65_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace65_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression replace65 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace65_0.value, &replace65_1.value },
};
   
static const nir_search_variable search66_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search66_1 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};

static const nir_search_constant search66_2 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};
static const nir_search_expression search66 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search66_0.value, &search66_1.value, &search66_2.value },
};
   
static const nir_search_variable replace66_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace66_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression replace66 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace66_0.value, &replace66_1.value },
};
   
static const nir_search_constant search67_0 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};

static const nir_search_variable search67_1 = {
   { nir_search_value_variable },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search67_2 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search67 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search67_0.value, &search67_1.value, &search67_2.value },
};
   
static const nir_search_variable replace67 = {
   { nir_search_value_variable },
   0, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search68_0 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};

static const nir_search_variable search68_1 = {
   { nir_search_value_variable },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search68_2 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search68 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search68_0.value, &search68_1.value, &search68_2.value },
};
   
static const nir_search_variable replace68 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search69_0 = {
   { nir_search_value_variable },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search69_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search69_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search69 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search69_0.value, &search69_1.value, &search69_2.value },
};
   
static const nir_search_variable replace69_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace69_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression replace69_0 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace69_0_0.value, &replace69_0_1.value },
};

static const nir_search_variable replace69_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace69_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace69 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace69_0.value, &replace69_1.value, &replace69_2.value },
};

static const struct transform nir_opt_algebraic_bcsel_xforms[] = {
   { &search65, &replace65.value },
   { &search66, &replace66.value },
   { &search67, &replace67.value },
   { &search68, &replace68.value },
   { &search69, &replace69.value },
};
   
static const nir_search_variable search7_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search7_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search7 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &search7_0.value, &search7_1.value },
};
   
static const nir_search_variable replace7 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search7, &replace7.value },
};
   
static const nir_search_variable search39_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search39_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search39 = {
   { nir_search_value_expression },
   nir_op_fxor,
   { &search39_0.value, &search39_1.value },
};
   
static const nir_search_constant replace39 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search39, &replace39.value },
};
   
static const nir_search_variable search2_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search2_0 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search2_0_0.value },
};
static const nir_search_expression search2 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search2_0.value },
};
   
static const nir_search_variable replace2_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace2 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &replace2_0.value },
};
   
static const nir_search_variable search3_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search3_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search3_0_0.value },
};
static const nir_search_expression search3 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search3_0.value },
};
   
static const nir_search_variable replace3_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace3 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &replace3_0.value },
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search2, &replace2.value },
   { &search3, &replace3.value },
};
   
static const nir_search_variable search64_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_bool,
};

static const nir_search_constant search64_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search64 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search64_0.value, &search64_1.value },
};
   
static const nir_search_variable replace64_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace64 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &replace64_0.value },
};
   
static const nir_search_variable search83_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search83_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search83_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search83_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search83_0_0.value, &search83_0_1.value, &search83_0_2.value },
};

static const nir_search_variable search83_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search83 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search83_0.value, &search83_1.value },
};
   
static const nir_search_variable replace83_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace83_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace83_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace83_1 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace83_1_0.value, &replace83_1_1.value },
};

static const nir_search_variable replace83_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace83_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace83_2 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace83_2_0.value, &replace83_2_1.value },
};
static const nir_search_expression replace83 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace83_0.value, &replace83_1.value, &replace83_2.value },
};
   
static const nir_search_variable search84_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search84_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search84_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search84_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search84_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search84_1_0.value, &search84_1_1.value, &search84_1_2.value },
};
static const nir_search_expression search84 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search84_0.value, &search84_1.value },
};
   
static const nir_search_variable replace84_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace84_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace84_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace84_1 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace84_1_0.value, &replace84_1_1.value },
};

static const nir_search_variable replace84_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace84_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace84_2 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace84_2_0.value, &replace84_2_1.value },
};
static const nir_search_expression replace84 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace84_0.value, &replace84_1.value, &replace84_2.value },
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search64, &replace64.value },
   { &search83, &replace83.value },
   { &search84, &replace84.value },
};
   
static const nir_search_variable search1_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search1_0 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &search1_0_0.value },
};
static const nir_search_expression search1 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &search1_0.value },
};
   
static const nir_search_variable replace1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search1, &replace1.value },
};
   
static const nir_search_variable search81_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search81_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search81_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search81_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search81_0_0.value, &search81_0_1.value, &search81_0_2.value },
};

static const nir_search_variable search81_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search81 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search81_0.value, &search81_1.value },
};
   
static const nir_search_variable replace81_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace81_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace81_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace81_1 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace81_1_0.value, &replace81_1_1.value },
};

static const nir_search_variable replace81_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace81_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace81_2 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace81_2_0.value, &replace81_2_1.value },
};
static const nir_search_expression replace81 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace81_0.value, &replace81_1.value, &replace81_2.value },
};
   
static const nir_search_variable search82_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search82_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search82_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search82_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search82_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search82_1_0.value, &search82_1_1.value, &search82_1_2.value },
};
static const nir_search_expression search82 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search82_0.value, &search82_1.value },
};
   
static const nir_search_variable replace82_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace82_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace82_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace82_1 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace82_1_0.value, &replace82_1_1.value },
};

static const nir_search_variable replace82_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace82_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace82_2 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace82_2_0.value, &replace82_2_1.value },
};
static const nir_search_expression replace82 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace82_0.value, &replace82_1.value, &replace82_2.value },
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search81, &replace81.value },
   { &search82, &replace82.value },
};
   
static const nir_search_constant search59_0 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search59_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search59 = {
   { nir_search_value_expression },
   nir_op_fdiv,
   { &search59_0.value, &search59_1.value },
};
   
static const nir_search_variable replace59_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace59 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &replace59_0.value },
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search59, &replace59.value },
};
   
static const nir_search_variable search30_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search30_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search30_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search30_0_0.value, &search30_0_1.value },
};

static const nir_search_constant search30_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search30 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search30_0.value, &search30_1.value },
};
   
static const nir_search_variable replace30_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace30_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace30_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace30_1_0.value },
};
static const nir_search_expression replace30 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace30_0.value, &replace30_1.value },
};
   
static const nir_search_variable search70_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search70_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search70_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search70_0_0.value, &search70_0_1.value },
};

static const nir_search_constant search70_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search70 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search70_0.value, &search70_1.value },
};
   
static const nir_search_variable replace70_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace70_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace70_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace70_1_0.value },
};
static const nir_search_expression replace70 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace70_0.value, &replace70_1.value },
};
   
static const nir_search_variable search75_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search75_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search75_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search75_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search75_0_0.value, &search75_0_1.value, &search75_0_2.value },
};

static const nir_search_variable search75_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search75 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search75_0.value, &search75_1.value },
};
   
static const nir_search_variable replace75_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace75_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace75_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace75_1 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace75_1_0.value, &replace75_1_1.value },
};

static const nir_search_variable replace75_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace75_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace75_2 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace75_2_0.value, &replace75_2_1.value },
};
static const nir_search_expression replace75 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace75_0.value, &replace75_1.value, &replace75_2.value },
};
   
static const nir_search_variable search76_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search76_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search76_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search76_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search76_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search76_1_0.value, &search76_1_1.value, &search76_1_2.value },
};
static const nir_search_expression search76 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search76_0.value, &search76_1.value },
};
   
static const nir_search_variable replace76_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace76_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace76_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace76_1 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace76_1_0.value, &replace76_1_1.value },
};

static const nir_search_variable replace76_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace76_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace76_2 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace76_2_0.value, &replace76_2_1.value },
};
static const nir_search_expression replace76 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace76_0.value, &replace76_1.value, &replace76_2.value },
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search30, &replace30.value },
   { &search70, &replace70.value },
   { &search75, &replace75.value },
   { &search76, &replace76.value },
};
   
static const nir_search_constant search44_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search44_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search44 = {
   { nir_search_value_expression },
   nir_op_ishl,
   { &search44_0.value, &search44_1.value },
};
   
static const nir_search_constant replace44 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search45_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search45_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search45 = {
   { nir_search_value_expression },
   nir_op_ishl,
   { &search45_0.value, &search45_1.value },
};
   
static const nir_search_variable replace45 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search44, &replace44.value },
   { &search45, &replace45.value },
};
   
static const nir_search_variable search52_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search52_0 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search52_0_0.value },
};
static const nir_search_expression search52 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search52_0.value },
};
   
static const nir_search_variable replace52 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search52, &replace52.value },
};
   
static const nir_search_variable search24_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search24_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search24_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search24_0_0.value, &search24_0_1.value },
};
static const nir_search_expression search24 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search24_0.value },
};
   
static const nir_search_variable replace24_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace24_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace24 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace24_0.value, &replace24_1.value },
};
   
static const nir_search_variable search25_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search25_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search25_0 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search25_0_0.value, &search25_0_1.value },
};
static const nir_search_expression search25 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search25_0.value },
};
   
static const nir_search_variable replace25_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace25_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace25 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace25_0.value, &replace25_1.value },
};
   
static const nir_search_variable search26_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search26_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search26_0 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search26_0_0.value, &search26_0_1.value },
};
static const nir_search_expression search26 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search26_0.value },
};
   
static const nir_search_variable replace26_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace26_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace26 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace26_0.value, &replace26_1.value },
};
   
static const nir_search_variable search27_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search27_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search27_0 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search27_0_0.value, &search27_0_1.value },
};
static const nir_search_expression search27 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search27_0.value },
};
   
static const nir_search_variable replace27_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace27_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace27 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace27_0.value, &replace27_1.value },
};
   
static const nir_search_variable search41_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search41_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search41_0_0.value },
};
static const nir_search_expression search41 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search41_0.value },
};
   
static const nir_search_variable replace41 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_inot_xforms[] = {
   { &search24, &replace24.value },
   { &search25, &replace25.value },
   { &search26, &replace26.value },
   { &search27, &replace27.value },
   { &search41, &replace41.value },
};
   
static const nir_search_variable search56_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search56_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search56 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search56_0.value, &search56_1.value },
};
   
static const nir_search_variable replace56 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search57_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search57_1 = {
   { nir_search_value_constant },
   { 0x40000000 /* 2.0 */ },
};
static const nir_search_expression search57 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search57_0.value, &search57_1.value },
};
   
static const nir_search_variable replace57_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace57_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace57 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace57_0.value, &replace57_1.value },
};
   
static const nir_search_constant search58_0 = {
   { nir_search_value_constant },
   { 0x40000000 /* 2.0 */ },
};

static const nir_search_variable search58_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search58 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search58_0.value, &search58_1.value },
};
   
static const nir_search_variable replace58_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace58 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace58_0.value },
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search56, &replace56.value },
   { &search57, &replace57.value },
   { &search58, &replace58.value },
};
   
static const nir_search_variable search6_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search6_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search6 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search6_0.value, &search6_1.value },
};
   
static const nir_search_variable replace6 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search23_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search23_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search23_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search23_0_0.value, &search23_0_1.value },
};

static const nir_search_variable search23_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search23 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search23_0.value, &search23_1.value },
};
   
static const nir_search_variable replace23_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace23_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace23_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace23 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &replace23_0.value, &replace23_1.value, &replace23_2.value },
};

static const struct transform nir_opt_algebraic_fadd_xforms[] = {
   { &search6, &replace6.value },
   { &search23, &replace23.value },
};
   
static const nir_search_variable search19_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search19_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search19_2 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search19 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search19_0.value, &search19_1.value, &search19_2.value },
};
   
static const nir_search_variable replace19 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search20_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search20_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search20_2 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search20 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search20_0.value, &search20_1.value, &search20_2.value },
};
   
static const nir_search_variable replace20 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search21_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search21_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search21_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search21 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search21_0.value, &search21_1.value, &search21_2.value },
};
   
static const nir_search_variable replace21 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search22_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search22_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search22_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search22 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search22_0.value, &search22_1.value, &search22_2.value },
};
   
static const nir_search_variable replace22_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace22_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace22 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace22_0.value, &replace22_1.value },
};

static const struct transform nir_opt_algebraic_flrp_xforms[] = {
   { &search19, &replace19.value },
   { &search20, &replace20.value },
   { &search21, &replace21.value },
   { &search22, &replace22.value },
};
   
static const nir_search_variable search37_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search37_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search37 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search37_0.value, &search37_1.value },
};
   
static const nir_search_variable replace37 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search38_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search38_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search38 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search38_0.value, &search38_1.value },
};
   
static const nir_search_variable replace38 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search43_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search43_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search43_0_0.value },
};

static const nir_search_variable search43_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search43_1 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search43_1_0.value },
};
static const nir_search_expression search43 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search43_0.value, &search43_1.value },
};
   
static const nir_search_variable replace43_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace43_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace43_0 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &replace43_0_0.value, &replace43_0_1.value },
};
static const nir_search_expression replace43 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &replace43_0.value },
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search37, &replace37.value },
   { &search38, &replace38.value },
   { &search43, &replace43.value },
};
   
static const nir_search_variable search29_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search29_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search29_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search29_0_0.value, &search29_0_1.value },
};

static const nir_search_constant search29_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search29 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search29_0.value, &search29_1.value },
};
   
static const nir_search_variable replace29_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace29_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace29_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace29_1_0.value },
};
static const nir_search_expression replace29 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace29_0.value, &replace29_1.value },
};
   
static const nir_search_variable search32_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search32_0_0 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search32_0_0_0.value },
};
static const nir_search_expression search32_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search32_0_0.value },
};

static const nir_search_constant search32_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search32 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search32_0.value, &search32_1.value },
};
   
static const nir_search_variable replace32_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace32_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace32 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace32_0.value, &replace32_1.value },
};
   
static const nir_search_variable search73_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search73_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search73_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search73_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search73_0_0.value, &search73_0_1.value, &search73_0_2.value },
};

static const nir_search_variable search73_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search73 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search73_0.value, &search73_1.value },
};
   
static const nir_search_variable replace73_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace73_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace73_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace73_1 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace73_1_0.value, &replace73_1_1.value },
};

static const nir_search_variable replace73_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace73_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace73_2 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace73_2_0.value, &replace73_2_1.value },
};
static const nir_search_expression replace73 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace73_0.value, &replace73_1.value, &replace73_2.value },
};
   
static const nir_search_variable search74_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search74_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search74_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search74_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search74_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search74_1_0.value, &search74_1_1.value, &search74_1_2.value },
};
static const nir_search_expression search74 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search74_0.value, &search74_1.value },
};
   
static const nir_search_variable replace74_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace74_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace74_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace74_1 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace74_1_0.value, &replace74_1_1.value },
};

static const nir_search_variable replace74_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace74_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace74_2 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace74_2_0.value, &replace74_2_1.value },
};
static const nir_search_expression replace74 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace74_0.value, &replace74_1.value, &replace74_2.value },
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search29, &replace29.value },
   { &search32, &replace32.value },
   { &search73, &replace73.value },
   { &search74, &replace74.value },
};
   
static const nir_search_variable search60_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search60_0 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search60_0_0.value },
};
static const nir_search_expression search60 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search60_0.value },
};
   
static const nir_search_variable replace60 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search61_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search61_0 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &search61_0_0.value },
};
static const nir_search_expression search61 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search61_0.value },
};
   
static const nir_search_variable replace61_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace61 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &replace61_0.value },
};
   
static const nir_search_variable search62_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search62_0 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &search62_0_0.value },
};
static const nir_search_expression search62 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search62_0.value },
};
   
static const nir_search_variable replace62_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace62 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &replace62_0.value },
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search60, &replace60.value },
   { &search61, &replace61.value },
   { &search62, &replace62.value },
};
   
static const nir_search_variable search34_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search34_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search34 = {
   { nir_search_value_expression },
   nir_op_fand,
   { &search34_0.value, &search34_1.value },
};
   
static const nir_search_constant replace34 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search34, &replace34.value },
};
   
static const nir_search_constant search48_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search48_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search48 = {
   { nir_search_value_expression },
   nir_op_ushr,
   { &search48_0.value, &search48_1.value },
};
   
static const nir_search_constant replace48 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search49_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search49_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search49 = {
   { nir_search_value_expression },
   nir_op_ushr,
   { &search49_0.value, &search49_1.value },
};
   
static const nir_search_variable replace49 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search48, &replace48.value },
   { &search49, &replace49.value },
};
   
static const nir_search_variable search50_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search50_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search50_0_0.value },
};
static const nir_search_expression search50 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search50_0.value },
};
   
static const nir_search_variable replace50 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search54_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search54_0_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search54_0_0_0.value },
};

static const nir_search_variable search54_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search54_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search54_0_0.value, &search54_0_1.value },
};
static const nir_search_expression search54 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search54_0.value },
};
   
static const nir_search_variable replace54_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace54_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace54 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &replace54_0.value, &replace54_1.value },
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search50, &replace50.value },
   { &search54, &replace54.value },
};
   
static const nir_search_constant search46_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search46_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search46 = {
   { nir_search_value_expression },
   nir_op_ishr,
   { &search46_0.value, &search46_1.value },
};
   
static const nir_search_constant replace46 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search47_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search47_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search47 = {
   { nir_search_value_expression },
   nir_op_ishr,
   { &search47_0.value, &search47_1.value },
};
   
static const nir_search_variable replace47 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search46, &replace46.value },
   { &search47, &replace47.value },
};
   
static const nir_search_variable search28_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search28_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search28_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search28_0_0.value, &search28_0_1.value },
};

static const nir_search_constant search28_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search28 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search28_0.value, &search28_1.value },
};
   
static const nir_search_variable replace28_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace28_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace28_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace28_1_0.value },
};
static const nir_search_expression replace28 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace28_0.value, &replace28_1.value },
};
   
static const nir_search_variable search71_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search71_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search71_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search71_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search71_0_0.value, &search71_0_1.value, &search71_0_2.value },
};

static const nir_search_variable search71_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search71 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search71_0.value, &search71_1.value },
};
   
static const nir_search_variable replace71_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace71_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace71_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace71_1 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace71_1_0.value, &replace71_1_1.value },
};

static const nir_search_variable replace71_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace71_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace71_2 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace71_2_0.value, &replace71_2_1.value },
};
static const nir_search_expression replace71 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace71_0.value, &replace71_1.value, &replace71_2.value },
};
   
static const nir_search_variable search72_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search72_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search72_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search72_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search72_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search72_1_0.value, &search72_1_1.value, &search72_1_2.value },
};
static const nir_search_expression search72 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search72_0.value, &search72_1.value },
};
   
static const nir_search_variable replace72_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace72_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace72_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace72_1 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace72_1_0.value, &replace72_1_1.value },
};

static const nir_search_variable replace72_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace72_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace72_2 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace72_2_0.value, &replace72_2_1.value },
};
static const nir_search_expression replace72 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace72_0.value, &replace72_1.value, &replace72_2.value },
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search28, &replace28.value },
   { &search71, &replace71.value },
   { &search72, &replace72.value },
};
   
static const nir_search_variable search87_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search87_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search87_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search87_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search87_0_0.value, &search87_0_1.value, &search87_0_2.value },
};

static const nir_search_variable search87_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search87 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &search87_0.value, &search87_1.value },
};
   
static const nir_search_variable replace87_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace87_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace87_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace87_1 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace87_1_0.value, &replace87_1_1.value },
};

static const nir_search_variable replace87_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace87_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace87_2 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace87_2_0.value, &replace87_2_1.value },
};
static const nir_search_expression replace87 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace87_0.value, &replace87_1.value, &replace87_2.value },
};
   
static const nir_search_variable search88_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search88_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search88_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search88_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search88_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search88_1_0.value, &search88_1_1.value, &search88_1_2.value },
};
static const nir_search_expression search88 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &search88_0.value, &search88_1.value },
};
   
static const nir_search_variable replace88_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace88_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace88_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace88_1 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace88_1_0.value, &replace88_1_1.value },
};

static const nir_search_variable replace88_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace88_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace88_2 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace88_2_0.value, &replace88_2_1.value },
};
static const nir_search_expression replace88 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace88_0.value, &replace88_1.value, &replace88_2.value },
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search87, &replace87.value },
   { &search88, &replace88.value },
};
   
static const nir_search_variable search0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search0_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search0_0_0.value },
};
static const nir_search_expression search0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search0_0.value },
};
   
static const nir_search_variable replace0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search0, &replace0.value },
};
   
static const nir_search_variable search31_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search31_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search31_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search31_0_0.value, &search31_0_1.value },
};

static const nir_search_constant search31_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search31 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search31_0.value, &search31_1.value },
};
   
static const nir_search_variable replace31_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace31_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace31_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace31_1_0.value },
};
static const nir_search_expression replace31 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace31_0.value, &replace31_1.value },
};
   
static const nir_search_variable search77_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search77_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search77_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search77_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search77_0_0.value, &search77_0_1.value, &search77_0_2.value },
};

static const nir_search_variable search77_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search77 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search77_0.value, &search77_1.value },
};
   
static const nir_search_variable replace77_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace77_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace77_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace77_1 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace77_1_0.value, &replace77_1_1.value },
};

static const nir_search_variable replace77_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace77_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace77_2 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace77_2_0.value, &replace77_2_1.value },
};
static const nir_search_expression replace77 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace77_0.value, &replace77_1.value, &replace77_2.value },
};
   
static const nir_search_variable search78_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search78_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search78_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search78_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search78_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search78_1_0.value, &search78_1_1.value, &search78_1_2.value },
};
static const nir_search_expression search78 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search78_0.value, &search78_1.value },
};
   
static const nir_search_variable replace78_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace78_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace78_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace78_1 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace78_1_0.value, &replace78_1_1.value },
};

static const nir_search_variable replace78_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace78_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace78_2 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace78_2_0.value, &replace78_2_1.value },
};
static const nir_search_expression replace78 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace78_0.value, &replace78_1.value, &replace78_2.value },
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search31, &replace31.value },
   { &search77, &replace77.value },
   { &search78, &replace78.value },
};
   
static const nir_search_variable search4_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search4_0 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &search4_0_0.value },
};
static const nir_search_expression search4 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &search4_0.value },
};
   
static const nir_search_variable replace4_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace4 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &replace4_0.value },
};
   
static const nir_search_variable search5_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search5_0 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &search5_0_0.value },
};
static const nir_search_expression search5 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &search5_0.value },
};
   
static const nir_search_variable replace5_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace5 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &replace5_0.value },
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search4, &replace4.value },
   { &search5, &replace5.value },
};
   
static const nir_search_variable search33_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search33_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search33_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &search33_0_0.value, &search33_0_1.value },
};

static const nir_search_constant search33_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search33 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &search33_0.value, &search33_1.value },
};
   
static const nir_search_variable replace33_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace33 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &replace33_0.value },
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search33, &replace33.value },
};
   
static const nir_search_variable search53_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search53_0 = {
   { nir_search_value_expression },
   nir_op_fexp,
   { &search53_0_0.value },
};
static const nir_search_expression search53 = {
   { nir_search_value_expression },
   nir_op_flog,
   { &search53_0.value },
};
   
static const nir_search_variable replace53 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_flog_xforms[] = {
   { &search53, &replace53.value },
};

struct opt_state {
   void *mem_ctx;
   bool progress;
};

static bool
nir_opt_algebraic_block(nir_block *block, void *void_state)
{
   struct opt_state *state = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!alu->dest.dest.is_ssa)
         continue;

      switch (alu->op) {
      case nir_op_iand:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iand_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iand_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fexp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fexp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fexp_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ixor:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ixor_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ixor_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ilt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ilt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ilt_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_imul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imul_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_uge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_uge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_uge_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fmul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmul_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ffma:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ffma_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ffma_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ine:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ine_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ine_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_bcsel:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_bcsel_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_bcsel_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_iadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iadd_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fxor:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fxor_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fxor_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fabs:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fabs_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fabs_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ieq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ieq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ieq_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ineg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ineg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ineg_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ige:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ige_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ige_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fdiv:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fdiv_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fdiv_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_feq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_feq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_feq_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ishl:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ishl_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ishl_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flog2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flog2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flog2_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_inot:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_inot_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_inot_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fpow:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fpow_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fpow_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fadd_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flrp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flrp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flrp_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ior:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ior_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ior_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fge_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_frcp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_frcp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_frcp_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fand:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fand_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fand_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ushr:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ushr_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ushr_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fexp2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fexp2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fexp2_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ishr:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ishr_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ishr_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flt_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ult:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ult_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ult_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fneg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fneg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fneg_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fne_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_iabs:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iabs_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iabs_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fmin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmin_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flog:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flog_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flog_xforms[i];
            if (nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      default:
         break;
      }
   }

   return true;
}

static bool
nir_opt_algebraic_impl(nir_function_impl *impl)
{
   struct opt_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.progress = false;

   nir_foreach_block(impl, nir_opt_algebraic_block, &state);

   if (state.progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return state.progress;
}

bool
nir_opt_algebraic(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress |= nir_opt_algebraic_impl(overload->impl);
   }

   return progress;
}

