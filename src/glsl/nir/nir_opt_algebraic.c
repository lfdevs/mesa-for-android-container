
#include "nir.h"
#include "nir_search.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

struct opt_state {
   void *mem_ctx;
   bool progress;
   const bool *condition_flags;
};

#endif

   
static const nir_search_variable search74_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_bool,
};

static const nir_search_constant search74_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search74 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search74_0.value, &search74_1.value },
};
   
static const nir_search_variable replace74_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace74 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace74_0.value },
};
   
static const nir_search_variable search84_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search84_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search84 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search84_0.value, &search84_1.value },
};
   
static const nir_search_variable replace84 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search85_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search85_1 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};
static const nir_search_expression search85 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search85_0.value, &search85_1.value },
};
   
static const nir_search_variable replace85 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search86_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search86_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search86 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search86_0.value, &search86_1.value },
};
   
static const nir_search_constant replace86 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search92_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search92_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search92_0_0.value },
};

static const nir_search_variable search92_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search92_1 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search92_1_0.value },
};
static const nir_search_expression search92 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &search92_0.value, &search92_1.value },
};
   
static const nir_search_variable replace92_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace92_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace92_0 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &replace92_0_0.value, &replace92_0_1.value },
};
static const nir_search_expression replace92 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &replace92_0.value },
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search74, &replace74.value, 0 },
   { &search84, &replace84.value, 0 },
   { &search85, &replace85.value, 0 },
   { &search86, &replace86.value, 0 },
   { &search92, &replace92.value, 0 },
};
   
static const nir_search_variable search90_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search90_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search90 = {
   { nir_search_value_expression },
   nir_op_ixor,
   { &search90_0.value, &search90_1.value },
};
   
static const nir_search_constant replace90 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search90, &replace90.value, 0 },
};
   
static const nir_search_variable search67_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search67_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search67 = {
   { nir_search_value_expression },
   nir_op_seq,
   { &search67_0.value, &search67_1.value },
};
   
static const nir_search_variable replace67_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace67_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace67_0 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace67_0_0.value, &replace67_0_1.value },
};
static const nir_search_expression replace67 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace67_0.value },
};

static const struct transform nir_opt_algebraic_seq_xforms[] = {
   { &search67, &replace67.value, 8 },
};
   
static const nir_search_variable search77_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search77_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search77 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search77_0.value, &search77_1.value },
};
   
static const nir_search_constant replace77 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search157_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search157_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search157_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search157_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search157_0_0.value, &search157_0_1.value, &search157_0_2.value },
};

static const nir_search_variable search157_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search157 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search157_0.value, &search157_1.value },
};
   
static const nir_search_variable replace157_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace157_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace157_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace157_1 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace157_1_0.value, &replace157_1_1.value },
};

static const nir_search_variable replace157_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace157_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace157_2 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace157_2_0.value, &replace157_2_1.value },
};
static const nir_search_expression replace157 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace157_0.value, &replace157_1.value, &replace157_2.value },
};
   
static const nir_search_variable search158_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search158_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search158_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search158_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search158_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search158_1_0.value, &search158_1_1.value, &search158_1_2.value },
};
static const nir_search_expression search158 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search158_0.value, &search158_1.value },
};
   
static const nir_search_variable replace158_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace158_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace158_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace158_1 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace158_1_0.value, &replace158_1_1.value },
};

static const nir_search_variable replace158_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace158_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace158_2 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace158_2_0.value, &replace158_2_1.value },
};
static const nir_search_expression replace158 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace158_0.value, &replace158_1.value, &replace158_2.value },
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search77, &replace77.value, 0 },
   { &search157, &replace157.value, 0 },
   { &search158, &replace158.value, 0 },
};
   
static const nir_search_variable search15_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search15_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search15 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search15_0.value, &search15_1.value },
};
   
static const nir_search_constant replace15 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search19_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search19_1 = {
   { nir_search_value_constant },
   { 0x1 /* 1 */ },
};
static const nir_search_expression search19 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search19_0.value, &search19_1.value },
};
   
static const nir_search_variable replace19 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search21_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search21_1 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};
static const nir_search_expression search21 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search21_0.value, &search21_1.value },
};
   
static const nir_search_variable replace21_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace21 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &replace21_0.value },
};
   
static const nir_search_variable search71_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search71_0 = {
   { nir_search_value_expression },
   nir_op_b2i,
   { &search71_0_0.value },
};

static const nir_search_variable search71_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search71_1 = {
   { nir_search_value_expression },
   nir_op_b2i,
   { &search71_1_0.value },
};
static const nir_search_expression search71 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search71_0.value, &search71_1.value },
};
   
static const nir_search_variable replace71_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace71_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace71_0 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &replace71_0_0.value, &replace71_0_1.value },
};
static const nir_search_expression replace71 = {
   { nir_search_value_expression },
   nir_op_b2i,
   { &replace71_0.value },
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search15, &replace15.value, 0 },
   { &search19, &replace19.value, 0 },
   { &search21, &replace21.value, 0 },
   { &search71, &replace71.value, 0 },
};
   
static const nir_search_variable search82_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search82_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search82 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &search82_0.value, &search82_1.value },
};
   
static const nir_search_constant replace82 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search167_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search167_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search167_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search167_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search167_0_0.value, &search167_0_1.value, &search167_0_2.value },
};

static const nir_search_variable search167_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search167 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &search167_0.value, &search167_1.value },
};
   
static const nir_search_variable replace167_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace167_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace167_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace167_1 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace167_1_0.value, &replace167_1_1.value },
};

static const nir_search_variable replace167_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace167_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace167_2 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace167_2_0.value, &replace167_2_1.value },
};
static const nir_search_expression replace167 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace167_0.value, &replace167_1.value, &replace167_2.value },
};
   
static const nir_search_variable search168_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search168_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search168_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search168_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search168_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search168_1_0.value, &search168_1_1.value, &search168_1_2.value },
};
static const nir_search_expression search168 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &search168_0.value, &search168_1.value },
};
   
static const nir_search_variable replace168_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace168_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace168_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace168_1 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace168_1_0.value, &replace168_1_1.value },
};

static const nir_search_variable replace168_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace168_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace168_2 = {
   { nir_search_value_expression },
   nir_op_uge,
   { &replace168_2_0.value, &replace168_2_1.value },
};
static const nir_search_expression replace168 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace168_0.value, &replace168_1.value, &replace168_2.value },
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search82, &replace82.value, 0 },
   { &search167, &replace167.value, 0 },
   { &search168, &replace168.value, 0 },
};
   
static const nir_search_variable search8_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search8_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search8 = {
   { nir_search_value_expression },
   nir_op_usadd_4x8,
   { &search8_0.value, &search8_1.value },
};
   
static const nir_search_variable replace8 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search9_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search9_1 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};
static const nir_search_expression search9 = {
   { nir_search_value_expression },
   nir_op_usadd_4x8,
   { &search9_0.value, &search9_1.value },
};
   
static const nir_search_constant replace9 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};

static const struct transform nir_opt_algebraic_usadd_4x8_xforms[] = {
   { &search8, &replace8.value, 0 },
   { &search9, &replace9.value, 0 },
};
   
static const nir_search_variable search14_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search14_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search14 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search14_0.value, &search14_1.value },
};
   
static const nir_search_constant replace14 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search18_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search18_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search18 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search18_0.value, &search18_1.value },
};
   
static const nir_search_variable replace18 = {
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

static const nir_search_constant search20_1 = {
   { nir_search_value_constant },
   { 0xbf800000 /* -1.0 */ },
};
static const nir_search_expression search20 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search20_0.value, &search20_1.value },
};
   
static const nir_search_variable replace20_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace20 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace20_0.value },
};
   
static const nir_search_variable search72_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search72_0 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &search72_0_0.value },
};

static const nir_search_variable search72_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search72_1 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &search72_1_0.value },
};
static const nir_search_expression search72 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search72_0.value, &search72_1.value },
};
   
static const nir_search_variable replace72_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace72_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace72_0 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &replace72_0_0.value, &replace72_0_1.value },
};
static const nir_search_expression replace72 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace72_0.value },
};
   
static const nir_search_variable search119_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search119_0 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search119_0_0.value },
};

static const nir_search_variable search119_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search119_1 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search119_1_0.value },
};
static const nir_search_expression search119 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search119_0.value, &search119_1.value },
};
   
static const nir_search_variable replace119_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace119_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace119_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace119_0_0.value, &replace119_0_1.value },
};
static const nir_search_expression replace119 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace119_0.value },
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search14, &replace14.value, 0 },
   { &search18, &replace18.value, 0 },
   { &search20, &replace20.value, 0 },
   { &search72, &replace72.value, 0 },
   { &search119, &replace119.value, 0 },
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
   nir_op_ffma,
   { &search22_0.value, &search22_1.value, &search22_2.value },
};
   
static const nir_search_variable replace22 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search23_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search23_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search23_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search23 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search23_0.value, &search23_1.value, &search23_2.value },
};
   
static const nir_search_variable replace23 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search24_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search24_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search24_2 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search24 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search24_0.value, &search24_1.value, &search24_2.value },
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
   nir_op_fmul,
   { &replace24_0.value, &replace24_1.value },
};
   
static const nir_search_variable search25_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search25_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search25_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search25 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search25_0.value, &search25_1.value, &search25_2.value },
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
   nir_op_fadd,
   { &replace25_0.value, &replace25_1.value },
};
   
static const nir_search_constant search26_0 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search26_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search26_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search26 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search26_0.value, &search26_1.value, &search26_2.value },
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
   nir_op_fadd,
   { &replace26_0.value, &replace26_1.value },
};
   
static const nir_search_variable search35_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search35_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search35_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search35 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &search35_0.value, &search35_1.value, &search35_2.value },
};
   
static const nir_search_variable replace35_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace35_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace35_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace35_0_0.value, &replace35_0_1.value },
};

static const nir_search_variable replace35_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace35 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace35_0.value, &replace35_1.value },
};

static const struct transform nir_opt_algebraic_ffma_xforms[] = {
   { &search22, &replace22.value, 0 },
   { &search23, &replace23.value, 0 },
   { &search24, &replace24.value, 0 },
   { &search25, &replace25.value, 0 },
   { &search26, &replace26.value, 0 },
   { &search35, &replace35.value, 4 },
};
   
static const nir_search_variable search54_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search54_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search54 = {
   { nir_search_value_expression },
   nir_op_umin,
   { &search54_0.value, &search54_1.value },
};
   
static const nir_search_variable replace54 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_umin_xforms[] = {
   { &search54, &replace54.value, 0 },
};
   
static const nir_search_variable search55_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search55_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search55 = {
   { nir_search_value_expression },
   nir_op_umax,
   { &search55_0.value, &search55_1.value },
};
   
static const nir_search_variable replace55 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_umax_xforms[] = {
   { &search55, &replace55.value, 0 },
};
   
static const nir_search_variable search46_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search46_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search46_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search46_0_0.value, &search46_0_1.value },
};

static const nir_search_variable search46_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search46_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search46 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search46_0.value, &search46_1.value, &search46_2.value },
};
   
static const nir_search_variable replace46_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace46_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace46 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &replace46_0.value, &replace46_1.value },
};
   
static const nir_search_variable search47_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search47_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search47_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search47_0_0.value, &search47_0_1.value },
};

static const nir_search_variable search47_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search47_2 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search47 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search47_0.value, &search47_1.value, &search47_2.value },
};
   
static const nir_search_variable replace47_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace47_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace47 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &replace47_0.value, &replace47_1.value },
};
   
static const nir_search_variable search48_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_bool,
};
static const nir_search_expression search48_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search48_0_0.value },
};

static const nir_search_variable search48_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search48_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search48 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search48_0.value, &search48_1.value, &search48_2.value },
};
   
static const nir_search_variable replace48_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace48_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace48_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace48 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace48_0.value, &replace48_1.value, &replace48_2.value },
};
   
static const nir_search_variable search49_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search49_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search49_1_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search49_1_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search49_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search49_1_0.value, &search49_1_1.value, &search49_1_2.value },
};

static const nir_search_variable search49_2 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression search49 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search49_0.value, &search49_1.value, &search49_2.value },
};
   
static const nir_search_variable replace49_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace49_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace49_2 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace49 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace49_0.value, &replace49_1.value, &replace49_2.value },
};
   
static const nir_search_variable search127_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search127_1 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};

static const nir_search_constant search127_2 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};
static const nir_search_expression search127 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search127_0.value, &search127_1.value, &search127_2.value },
};
   
static const nir_search_variable replace127_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace127_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression replace127 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace127_0.value, &replace127_1.value },
};
   
static const nir_search_variable search128_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search128_1 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};

static const nir_search_constant search128_2 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};
static const nir_search_expression search128 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search128_0.value, &search128_1.value, &search128_2.value },
};
   
static const nir_search_variable replace128_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace128_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression replace128 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace128_0.value, &replace128_1.value },
};
   
static const nir_search_constant search129_0 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};

static const nir_search_variable search129_1 = {
   { nir_search_value_variable },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search129_2 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search129 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search129_0.value, &search129_1.value, &search129_2.value },
};
   
static const nir_search_variable replace129 = {
   { nir_search_value_variable },
   0, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search130_0 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};

static const nir_search_variable search130_1 = {
   { nir_search_value_variable },
   0, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search130_2 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search130 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search130_0.value, &search130_1.value, &search130_2.value },
};
   
static const nir_search_variable replace130 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search131_0 = {
   { nir_search_value_variable },
   0, /* a */
   true,
   nir_type_invalid,
};

static const nir_search_variable search131_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search131_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search131 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search131_0.value, &search131_1.value, &search131_2.value },
};
   
static const nir_search_variable replace131_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace131_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression replace131_0 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace131_0_0.value, &replace131_0_1.value },
};

static const nir_search_variable replace131_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace131_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace131 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace131_0.value, &replace131_1.value, &replace131_2.value },
};
   
static const nir_search_variable search132_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search132_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search132_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search132 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search132_0.value, &search132_1.value, &search132_2.value },
};
   
static const nir_search_variable replace132 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_bcsel_xforms[] = {
   { &search46, &replace46.value, 0 },
   { &search47, &replace47.value, 0 },
   { &search48, &replace48.value, 0 },
   { &search49, &replace49.value, 0 },
   { &search127, &replace127.value, 0 },
   { &search128, &replace128.value, 0 },
   { &search129, &replace129.value, 0 },
   { &search130, &replace130.value, 0 },
   { &search131, &replace131.value, 0 },
   { &search132, &replace132.value, 0 },
};
   
static const nir_search_variable search66_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search66_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search66 = {
   { nir_search_value_expression },
   nir_op_sge,
   { &search66_0.value, &search66_1.value },
};
   
static const nir_search_variable replace66_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace66_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace66_0 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace66_0_0.value, &replace66_0_1.value },
};
static const nir_search_expression replace66 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace66_0.value },
};

static const struct transform nir_opt_algebraic_sge_xforms[] = {
   { &search66, &replace66.value, 8 },
};
   
static const nir_search_variable search110_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search110_0 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search110_0_0.value },
};
static const nir_search_expression search110 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &search110_0.value },
};
   
static const nir_search_constant replace110_0_0 = {
   { nir_search_value_constant },
   { 0x3f000000 /* 0.5 */ },
};

static const nir_search_variable replace110_0_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace110_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace110_0_0.value, &replace110_0_1.value },
};
static const nir_search_expression replace110 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace110_0.value },
};
   
static const nir_search_variable search123_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search123 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &search123_0.value },
};
   
static const nir_search_variable replace123_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace123_0 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &replace123_0_0.value },
};
static const nir_search_expression replace123 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &replace123_0.value },
};

static const struct transform nir_opt_algebraic_fsqrt_xforms[] = {
   { &search110, &replace110.value, 0 },
   { &search123, &replace123.value, 11 },
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
   
static const nir_search_variable search11_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search11_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search11_0 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search11_0_0.value, &search11_0_1.value },
};

static const nir_search_variable search11_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search11_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search11_1 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &search11_1_0.value, &search11_1_1.value },
};
static const nir_search_expression search11 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &search11_0.value, &search11_1.value },
};
   
static const nir_search_variable replace11_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace11_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace11_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace11_1 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &replace11_1_0.value, &replace11_1_1.value },
};
static const nir_search_expression replace11 = {
   { nir_search_value_expression },
   nir_op_imul,
   { &replace11_0.value, &replace11_1.value },
};
   
static const nir_search_variable search13_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search13_0 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &search13_0_0.value },
};

static const nir_search_variable search13_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search13 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &search13_0.value, &search13_1.value },
};
   
static const nir_search_constant replace13 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search146_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search146_1_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search146_1_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search146_1 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &search146_1_0.value, &search146_1_1.value },
};
static const nir_search_expression search146 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &search146_0.value, &search146_1.value },
};
   
static const nir_search_variable replace146_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace146_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace146 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &replace146_0.value, &replace146_1.value },
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search7, &replace7.value, 0 },
   { &search11, &replace11.value, 0 },
   { &search13, &replace13.value, 0 },
   { &search146, &replace146.value, 0 },
};
   
static const nir_search_variable search83_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search83_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search83 = {
   { nir_search_value_expression },
   nir_op_fand,
   { &search83_0.value, &search83_1.value },
};
   
static const nir_search_constant replace83 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search83, &replace83.value, 0 },
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
   
static const nir_search_constant search147_0_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search147_0_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search147_0 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &search147_0_0.value, &search147_0_1.value },
};
static const nir_search_expression search147 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search147_0.value },
};
   
static const nir_search_variable replace147_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace147 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &replace147_0.value },
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search2, &replace2.value, 0 },
   { &search3, &replace3.value, 0 },
   { &search147, &replace147.value, 0 },
};
   
static const nir_search_variable search79_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search79_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search79 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search79_0.value, &search79_1.value },
};
   
static const nir_search_constant replace79 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search126_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_bool,
};

static const nir_search_constant search126_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search126 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search126_0.value, &search126_1.value },
};
   
static const nir_search_variable replace126_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace126 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &replace126_0.value },
};
   
static const nir_search_variable search161_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search161_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search161_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search161_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search161_0_0.value, &search161_0_1.value, &search161_0_2.value },
};

static const nir_search_variable search161_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search161 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search161_0.value, &search161_1.value },
};
   
static const nir_search_variable replace161_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace161_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace161_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace161_1 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace161_1_0.value, &replace161_1_1.value },
};

static const nir_search_variable replace161_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace161_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace161_2 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace161_2_0.value, &replace161_2_1.value },
};
static const nir_search_expression replace161 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace161_0.value, &replace161_1.value, &replace161_2.value },
};
   
static const nir_search_variable search162_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search162_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search162_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search162_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search162_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search162_1_0.value, &search162_1_1.value, &search162_1_2.value },
};
static const nir_search_expression search162 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search162_0.value, &search162_1.value },
};
   
static const nir_search_variable replace162_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace162_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace162_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace162_1 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace162_1_0.value, &replace162_1_1.value },
};

static const nir_search_variable replace162_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace162_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace162_2 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace162_2_0.value, &replace162_2_1.value },
};
static const nir_search_expression replace162 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace162_0.value, &replace162_1.value, &replace162_2.value },
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search79, &replace79.value, 0 },
   { &search126, &replace126.value, 0 },
   { &search161, &replace161.value, 0 },
   { &search162, &replace162.value, 0 },
};
   
static const nir_search_variable search52_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search52_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search52 = {
   { nir_search_value_expression },
   nir_op_imin,
   { &search52_0.value, &search52_1.value },
};
   
static const nir_search_variable replace52 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_imin_xforms[] = {
   { &search52, &replace52.value, 0 },
};
   
static const nir_search_variable search112_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search112_0 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search112_0_0.value },
};
static const nir_search_expression search112 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &search112_0.value },
};
   
static const nir_search_constant replace112_0_0 = {
   { nir_search_value_constant },
   { 0xbf000000 /* -0.5 */ },
};

static const nir_search_variable replace112_0_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace112_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace112_0_0.value, &replace112_0_1.value },
};
static const nir_search_expression replace112 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace112_0.value },
};

static const struct transform nir_opt_algebraic_frsq_xforms[] = {
   { &search112, &replace112.value, 0 },
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
   
static const nir_search_variable search144_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search144 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &search144_0.value },
};
   
static const nir_search_constant replace144_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable replace144_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace144 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &replace144_0.value, &replace144_1.value },
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search1, &replace1.value, 0 },
   { &search144, &replace144.value, 14 },
};
   
static const nir_search_variable search102_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search102_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search102 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search102_0.value, &search102_1.value },
};
   
static const nir_search_variable replace102_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace102_0_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace102_0_0_0.value },
};

static const nir_search_variable replace102_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace102_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace102_0_0.value, &replace102_0_1.value },
};
static const nir_search_expression replace102 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace102_0.value },
};
   
static const nir_search_variable search104_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search104_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search104 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search104_0.value, &search104_1.value },
};
   
static const nir_search_variable replace104 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search105_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search105_1 = {
   { nir_search_value_constant },
   { 0x40000000 /* 2.0 */ },
};
static const nir_search_expression search105 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search105_0.value, &search105_1.value },
};
   
static const nir_search_variable replace105_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace105_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace105 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace105_0.value, &replace105_1.value },
};
   
static const nir_search_variable search106_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search106_1 = {
   { nir_search_value_constant },
   { 0x40800000 /* 4.0 */ },
};
static const nir_search_expression search106 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search106_0.value, &search106_1.value },
};
   
static const nir_search_variable replace106_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace106_0_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace106_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace106_0_0.value, &replace106_0_1.value },
};

static const nir_search_variable replace106_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace106_1_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace106_1 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace106_1_0.value, &replace106_1_1.value },
};
static const nir_search_expression replace106 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace106_0.value, &replace106_1.value },
};
   
static const nir_search_constant search107_0 = {
   { nir_search_value_constant },
   { 0x40000000 /* 2.0 */ },
};

static const nir_search_variable search107_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search107 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search107_0.value, &search107_1.value },
};
   
static const nir_search_variable replace107_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace107 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace107_0.value },
};
   
static const nir_search_variable search108_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search108_0_1 = {
   { nir_search_value_constant },
   { 0x400ccccd /* 2.2 */ },
};
static const nir_search_expression search108_0 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search108_0_0.value, &search108_0_1.value },
};

static const nir_search_constant search108_1 = {
   { nir_search_value_constant },
   { 0x3ee8ba1f /* 0.454545 */ },
};
static const nir_search_expression search108 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search108_0.value, &search108_1.value },
};
   
static const nir_search_variable replace108 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search109_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search109_0_0_1 = {
   { nir_search_value_constant },
   { 0x400ccccd /* 2.2 */ },
};
static const nir_search_expression search109_0_0 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search109_0_0_0.value, &search109_0_0_1.value },
};
static const nir_search_expression search109_0 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search109_0_0.value },
};

static const nir_search_constant search109_1 = {
   { nir_search_value_constant },
   { 0x3ee8ba1f /* 0.454545 */ },
};
static const nir_search_expression search109 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search109_0.value, &search109_1.value },
};
   
static const nir_search_variable replace109_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace109 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &replace109_0.value },
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search102, &replace102.value, 9 },
   { &search104, &replace104.value, 0 },
   { &search105, &replace105.value, 0 },
   { &search106, &replace106.value, 0 },
   { &search107, &replace107.value, 0 },
   { &search108, &replace108.value, 0 },
   { &search109, &replace109.value, 0 },
};
   
static const nir_search_variable search78_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search78_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search78 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search78_0.value, &search78_1.value },
};
   
static const nir_search_constant replace78 = {
   { nir_search_value_constant },
   { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search159_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search159_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search159_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search159_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search159_0_0.value, &search159_0_1.value, &search159_0_2.value },
};

static const nir_search_variable search159_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search159 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search159_0.value, &search159_1.value },
};
   
static const nir_search_variable replace159_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace159_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace159_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace159_1 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace159_1_0.value, &replace159_1_1.value },
};

static const nir_search_variable replace159_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace159_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace159_2 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace159_2_0.value, &replace159_2_1.value },
};
static const nir_search_expression replace159 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace159_0.value, &replace159_1.value, &replace159_2.value },
};
   
static const nir_search_variable search160_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search160_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search160_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search160_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search160_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search160_1_0.value, &search160_1_1.value, &search160_1_2.value },
};
static const nir_search_expression search160 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search160_0.value, &search160_1.value },
};
   
static const nir_search_variable replace160_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace160_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace160_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace160_1 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace160_1_0.value, &replace160_1_1.value },
};

static const nir_search_variable replace160_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace160_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace160_2 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace160_2_0.value, &replace160_2_1.value },
};
static const nir_search_expression replace160 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace160_0.value, &replace160_1.value, &replace160_2.value },
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search78, &replace78.value, 0 },
   { &search159, &replace159.value, 0 },
   { &search160, &replace160.value, 0 },
};
   
static const nir_search_constant search120_0 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search120_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search120 = {
   { nir_search_value_expression },
   nir_op_fdiv,
   { &search120_0.value, &search120_1.value },
};
   
static const nir_search_variable replace120_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace120 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &replace120_0.value },
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search120, &replace120.value, 0 },
};
   
static const nir_search_variable search32_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search32 = {
   { nir_search_value_expression },
   nir_op_ffract,
   { &search32_0.value },
};
   
static const nir_search_variable replace32_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace32_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace32_1 = {
   { nir_search_value_expression },
   nir_op_ffloor,
   { &replace32_1_0.value },
};
static const nir_search_expression replace32 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &replace32_0.value, &replace32_1.value },
};

static const struct transform nir_opt_algebraic_ffract_xforms[] = {
   { &search32, &replace32.value, 2 },
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
   
static const nir_search_variable search10_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search10_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search10_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search10_0_0.value, &search10_0_1.value },
};

static const nir_search_variable search10_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search10_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search10_1 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search10_1_0.value, &search10_1_1.value },
};
static const nir_search_expression search10 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search10_0.value, &search10_1.value },
};
   
static const nir_search_variable replace10_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace10_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace10_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace10_1 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace10_1_0.value, &replace10_1_1.value },
};
static const nir_search_expression replace10 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace10_0.value, &replace10_1.value },
};
   
static const nir_search_variable search12_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search12_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search12_0_0.value },
};

static const nir_search_variable search12_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search12 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search12_0.value, &search12_1.value },
};
   
static const nir_search_constant replace12 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
   
static const nir_search_variable search33_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search33_0_1_0 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};

static const nir_search_variable search33_0_1_1_0 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search33_0_1_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search33_0_1_1_0.value },
};
static const nir_search_expression search33_0_1 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search33_0_1_0.value, &search33_0_1_1.value },
};
static const nir_search_expression search33_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search33_0_0.value, &search33_0_1.value },
};

static const nir_search_variable search33_1_0 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search33_1_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search33_1 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search33_1_0.value, &search33_1_1.value },
};
static const nir_search_expression search33 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search33_0.value, &search33_1.value },
};
   
static const nir_search_variable replace33_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace33_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace33_2 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace33 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &replace33_0.value, &replace33_1.value, &replace33_2.value },
};
   
static const nir_search_variable search34_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search34_1_0 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable search34_1_1_0 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search34_1_1_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search34_1_1_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search34_1_1_1_0.value },
};
static const nir_search_expression search34_1_1 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search34_1_1_0.value, &search34_1_1_1.value },
};
static const nir_search_expression search34_1 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search34_1_0.value, &search34_1_1.value },
};
static const nir_search_expression search34 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search34_0.value, &search34_1.value },
};
   
static const nir_search_variable replace34_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace34_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace34_2 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace34 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &replace34_0.value, &replace34_1.value, &replace34_2.value },
};
   
static const nir_search_variable search36_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search36_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search36_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search36_0_0.value, &search36_0_1.value },
};

static const nir_search_variable search36_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search36 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search36_0.value, &search36_1.value },
};
   
static const nir_search_variable replace36_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace36_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace36_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace36 = {
   { nir_search_value_expression },
   nir_op_ffma,
   { &replace36_0.value, &replace36_1.value, &replace36_2.value },
};
   
static const nir_search_variable search117_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search117_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search117_0_0.value },
};

static const nir_search_variable search117_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search117_1 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search117_1_0.value },
};
static const nir_search_expression search117 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search117_0.value, &search117_1.value },
};
   
static const nir_search_variable replace117_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace117_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace117_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace117_0_0.value, &replace117_0_1.value },
};
static const nir_search_expression replace117 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace117_0.value },
};
   
static const nir_search_variable search118_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search118_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search118_0_0.value },
};

static const nir_search_variable search118_1_0_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search118_1_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search118_1_0_0.value },
};
static const nir_search_expression search118_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search118_1_0.value },
};
static const nir_search_expression search118 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search118_0.value, &search118_1.value },
};
   
static const nir_search_variable replace118_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace118_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace118_0 = {
   { nir_search_value_expression },
   nir_op_fdiv,
   { &replace118_0_0.value, &replace118_0_1.value },
};
static const nir_search_expression replace118 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace118_0.value },
};
   
static const nir_search_variable search145_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search145_1_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search145_1_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search145_1 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &search145_1_0.value, &search145_1_1.value },
};
static const nir_search_expression search145 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search145_0.value, &search145_1.value },
};
   
static const nir_search_variable replace145_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace145_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace145 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &replace145_0.value, &replace145_1.value },
};

static const struct transform nir_opt_algebraic_fadd_xforms[] = {
   { &search6, &replace6.value, 0 },
   { &search10, &replace10.value, 0 },
   { &search12, &replace12.value, 0 },
   { &search33, &replace33.value, 3 },
   { &search34, &replace34.value, 3 },
   { &search36, &replace36.value, 5 },
   { &search117, &replace117.value, 0 },
   { &search118, &replace118.value, 0 },
   { &search145, &replace145.value, 0 },
};
   
static const nir_search_constant search94_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search94_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search94 = {
   { nir_search_value_expression },
   nir_op_ishl,
   { &search94_0.value, &search94_1.value },
};
   
static const nir_search_constant replace94 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search95_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search95_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search95 = {
   { nir_search_value_expression },
   nir_op_ishl,
   { &search95_0.value, &search95_1.value },
};
   
static const nir_search_variable replace95 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search94, &replace94.value, 0 },
   { &search95, &replace95.value, 0 },
};
   
static const nir_search_variable search101_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search101_0 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search101_0_0.value },
};
static const nir_search_expression search101 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search101_0.value },
};
   
static const nir_search_variable replace101 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search113_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search113_0 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &search113_0_0.value },
};
static const nir_search_expression search113 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search113_0.value },
};
   
static const nir_search_constant replace113_0 = {
   { nir_search_value_constant },
   { 0x3f000000 /* 0.5 */ },
};

static const nir_search_variable replace113_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace113_1 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace113_1_0.value },
};
static const nir_search_expression replace113 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace113_0.value, &replace113_1.value },
};
   
static const nir_search_variable search114_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search114_0 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search114_0_0.value },
};
static const nir_search_expression search114 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search114_0.value },
};
   
static const nir_search_variable replace114_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace114_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace114_0_0.value },
};
static const nir_search_expression replace114 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace114_0.value },
};
   
static const nir_search_variable search115_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search115_0 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &search115_0_0.value },
};
static const nir_search_expression search115 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search115_0.value },
};
   
static const nir_search_constant replace115_0 = {
   { nir_search_value_constant },
   { 0xbf000000 /* -0.5 */ },
};

static const nir_search_variable replace115_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace115_1 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace115_1_0.value },
};
static const nir_search_expression replace115 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace115_0.value, &replace115_1.value },
};
   
static const nir_search_variable search116_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search116_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search116_0 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &search116_0_0.value, &search116_0_1.value },
};
static const nir_search_expression search116 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search116_0.value },
};
   
static const nir_search_variable replace116_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace116_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace116_1 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &replace116_1_0.value },
};
static const nir_search_expression replace116 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace116_0.value, &replace116_1.value },
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search101, &replace101.value, 0 },
   { &search113, &replace113.value, 0 },
   { &search114, &replace114.value, 0 },
   { &search115, &replace115.value, 0 },
   { &search116, &replace116.value, 0 },
};
   
static const nir_search_variable search37_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search37_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search37_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search37_0_0.value, &search37_0_1.value },
};
static const nir_search_expression search37 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search37_0.value },
};
   
static const nir_search_variable replace37_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace37_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace37 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace37_0.value, &replace37_1.value },
};
   
static const nir_search_variable search38_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search38_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search38_0 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search38_0_0.value, &search38_0_1.value },
};
static const nir_search_expression search38 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search38_0.value },
};
   
static const nir_search_variable replace38_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace38_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace38 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace38_0.value, &replace38_1.value },
};
   
static const nir_search_variable search39_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search39_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search39_0 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search39_0_0.value, &search39_0_1.value },
};
static const nir_search_expression search39 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search39_0.value },
};
   
static const nir_search_variable replace39_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace39_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace39 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace39_0.value, &replace39_1.value },
};
   
static const nir_search_variable search40_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search40_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search40_0 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search40_0_0.value, &search40_0_1.value },
};
static const nir_search_expression search40 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search40_0.value },
};
   
static const nir_search_variable replace40_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace40_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace40 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace40_0.value, &replace40_1.value },
};
   
static const nir_search_variable search41_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search41_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search41_0 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &search41_0_0.value, &search41_0_1.value },
};
static const nir_search_expression search41 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search41_0.value },
};
   
static const nir_search_variable replace41_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace41_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace41 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &replace41_0.value, &replace41_1.value },
};
   
static const nir_search_variable search42_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search42_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search42_0 = {
   { nir_search_value_expression },
   nir_op_ige,
   { &search42_0_0.value, &search42_0_1.value },
};
static const nir_search_expression search42 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search42_0.value },
};
   
static const nir_search_variable replace42_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace42_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace42 = {
   { nir_search_value_expression },
   nir_op_ilt,
   { &replace42_0.value, &replace42_1.value },
};
   
static const nir_search_variable search43_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search43_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search43_0 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &search43_0_0.value, &search43_0_1.value },
};
static const nir_search_expression search43 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search43_0.value },
};
   
static const nir_search_variable replace43_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace43_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace43 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace43_0.value, &replace43_1.value },
};
   
static const nir_search_variable search44_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search44_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search44_0 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search44_0_0.value, &search44_0_1.value },
};
static const nir_search_expression search44 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search44_0.value },
};
   
static const nir_search_variable replace44_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace44_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace44 = {
   { nir_search_value_expression },
   nir_op_ieq,
   { &replace44_0.value, &replace44_1.value },
};
   
static const nir_search_variable search91_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search91_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search91_0_0.value },
};
static const nir_search_expression search91 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search91_0.value },
};
   
static const nir_search_variable replace91 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_inot_xforms[] = {
   { &search37, &replace37.value, 0 },
   { &search38, &replace38.value, 0 },
   { &search39, &replace39.value, 0 },
   { &search40, &replace40.value, 0 },
   { &search41, &replace41.value, 0 },
   { &search42, &replace42.value, 0 },
   { &search43, &replace43.value, 0 },
   { &search44, &replace44.value, 0 },
   { &search91, &replace91.value, 0 },
};
   
static const nir_search_variable search68_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search68_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search68 = {
   { nir_search_value_expression },
   nir_op_sne,
   { &search68_0.value, &search68_1.value },
};
   
static const nir_search_variable replace68_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace68_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace68_0 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace68_0_0.value, &replace68_0_1.value },
};
static const nir_search_expression replace68 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace68_0.value },
};

static const struct transform nir_opt_algebraic_sne_xforms[] = {
   { &search68, &replace68.value, 8 },
};
   
static const nir_search_variable search136_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search136_0 = {
   { nir_search_value_expression },
   nir_op_ftrunc,
   { &search136_0_0.value },
};
static const nir_search_expression search136 = {
   { nir_search_value_expression },
   nir_op_f2u,
   { &search136_0.value },
};
   
static const nir_search_variable replace136_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace136 = {
   { nir_search_value_expression },
   nir_op_f2u,
   { &replace136_0.value },
};

static const struct transform nir_opt_algebraic_f2u_xforms[] = {
   { &search136, &replace136.value, 0 },
};
   
static const nir_search_variable search133_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search133_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search133_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search133 = {
   { nir_search_value_expression },
   nir_op_fcsel,
   { &search133_0.value, &search133_1.value, &search133_2.value },
};
   
static const nir_search_variable replace133 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_fcsel_xforms[] = {
   { &search133, &replace133.value, 0 },
};
   
static const nir_search_variable search138_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search138_1_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search138_1_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search138_1 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &search138_1_0.value, &search138_1_1.value },
};
static const nir_search_expression search138 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &search138_0.value, &search138_1.value },
};
   
static const nir_search_variable replace138_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace138_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace138 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &replace138_0.value, &replace138_1.value },
};
   
static const nir_search_variable search142_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search142_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search142 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &search142_0.value, &search142_1.value },
};
   
static const nir_search_variable replace142_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace142_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace142_1 = {
   { nir_search_value_expression },
   nir_op_ineg,
   { &replace142_1_0.value },
};
static const nir_search_expression replace142 = {
   { nir_search_value_expression },
   nir_op_iadd,
   { &replace142_0.value, &replace142_1.value },
};

static const struct transform nir_opt_algebraic_isub_xforms[] = {
   { &search138, &replace138.value, 0 },
   { &search142, &replace142.value, 13 },
};
   
static const nir_search_variable search51_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search51_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search51 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &search51_0.value, &search51_1.value },
};
   
static const nir_search_variable replace51 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search57_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search57_0_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search57_0 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &search57_0_0.value, &search57_0_1.value },
};

static const nir_search_constant search57_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search57 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &search57_0.value, &search57_1.value },
};
   
static const nir_search_variable replace57_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace57 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &replace57_0.value },
};

static const struct transform nir_opt_algebraic_fmax_xforms[] = {
   { &search51, &replace51.value, 0 },
   { &search57, &replace57.value, 6 },
};
   
static const nir_search_variable search16_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search16_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search16 = {
   { nir_search_value_expression },
   nir_op_umul_unorm_4x8,
   { &search16_0.value, &search16_1.value },
};
   
static const nir_search_constant replace16 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search17_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search17_1 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};
static const nir_search_expression search17 = {
   { nir_search_value_expression },
   nir_op_umul_unorm_4x8,
   { &search17_0.value, &search17_1.value },
};
   
static const nir_search_variable replace17 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_umul_unorm_4x8_xforms[] = {
   { &search16, &replace16.value, 0 },
   { &search17, &replace17.value, 0 },
};
   
static const nir_search_variable search70_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search70_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search70_0_0.value },
};

static const nir_search_variable search70_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
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

static const nir_search_constant replace70_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace70 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace70_0.value, &replace70_1.value },
};
   
static const nir_search_variable search153_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search153_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search153_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search153_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search153_0_0.value, &search153_0_1.value, &search153_0_2.value },
};

static const nir_search_variable search153_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search153 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search153_0.value, &search153_1.value },
};
   
static const nir_search_variable replace153_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace153_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace153_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace153_1 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace153_1_0.value, &replace153_1_1.value },
};

static const nir_search_variable replace153_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace153_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace153_2 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace153_2_0.value, &replace153_2_1.value },
};
static const nir_search_expression replace153 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace153_0.value, &replace153_1.value, &replace153_2.value },
};
   
static const nir_search_variable search154_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search154_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search154_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search154_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search154_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search154_1_0.value, &search154_1_1.value, &search154_1_2.value },
};
static const nir_search_expression search154 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search154_0.value, &search154_1.value },
};
   
static const nir_search_variable replace154_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace154_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace154_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace154_1 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace154_1_0.value, &replace154_1_1.value },
};

static const nir_search_variable replace154_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace154_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace154_2 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace154_2_0.value, &replace154_2_1.value },
};
static const nir_search_expression replace154 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace154_0.value, &replace154_1.value, &replace154_2.value },
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search70, &replace70.value, 0 },
   { &search153, &replace153.value, 0 },
   { &search154, &replace154.value, 0 },
};
   
static const nir_search_variable search27_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search27_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search27_2 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search27 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search27_0.value, &search27_1.value, &search27_2.value },
};
   
static const nir_search_variable replace27 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search28_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search28_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_constant search28_2 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search28 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search28_0.value, &search28_1.value, &search28_2.value },
};
   
static const nir_search_variable replace28 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search29_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search29_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search29_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search29 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search29_0.value, &search29_1.value, &search29_2.value },
};
   
static const nir_search_variable replace29 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search30_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search30_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search30_2 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search30 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search30_0.value, &search30_1.value, &search30_2.value },
};
   
static const nir_search_variable replace30_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace30_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace30 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace30_0.value, &replace30_1.value },
};
   
static const nir_search_variable search31_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search31_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search31_2 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search31 = {
   { nir_search_value_expression },
   nir_op_flrp,
   { &search31_0.value, &search31_1.value, &search31_2.value },
};
   
static const nir_search_variable replace31_0_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace31_0_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace31_0_1_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace31_0_1 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &replace31_0_1_0.value, &replace31_0_1_1.value },
};
static const nir_search_expression replace31_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &replace31_0_0.value, &replace31_0_1.value },
};

static const nir_search_variable replace31_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace31 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace31_0.value, &replace31_1.value },
};

static const struct transform nir_opt_algebraic_flrp_xforms[] = {
   { &search27, &replace27.value, 0 },
   { &search28, &replace28.value, 0 },
   { &search29, &replace29.value, 0 },
   { &search30, &replace30.value, 0 },
   { &search31, &replace31.value, 1 },
};
   
static const nir_search_variable search61_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search61_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search61_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search61_0_0.value, &search61_0_1.value },
};

static const nir_search_variable search61_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search61_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search61_1 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search61_1_0.value, &search61_1_1.value },
};
static const nir_search_expression search61 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search61_0.value, &search61_1.value },
};
   
static const nir_search_variable replace61_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace61_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace61_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace61_1 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &replace61_1_0.value, &replace61_1_1.value },
};
static const nir_search_expression replace61 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace61_0.value, &replace61_1.value },
};
   
static const nir_search_variable search62_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search62_0_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search62_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search62_0_0.value, &search62_0_1.value },
};

static const nir_search_variable search62_1_0 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search62_1_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search62_1 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search62_1_0.value, &search62_1_1.value },
};
static const nir_search_expression search62 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search62_0.value, &search62_1.value },
};
   
static const nir_search_variable replace62_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace62_0_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace62_0 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &replace62_0_0.value, &replace62_0_1.value },
};

static const nir_search_variable replace62_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace62 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace62_0.value, &replace62_1.value },
};
   
static const nir_search_variable search63_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search63_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search63_0 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search63_0_0.value, &search63_0_1.value },
};

static const nir_search_variable search63_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search63_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search63_1 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search63_1_0.value, &search63_1_1.value },
};
static const nir_search_expression search63 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search63_0.value, &search63_1.value },
};
   
static const nir_search_variable replace63_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace63_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace63_1_1 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace63_1 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &replace63_1_0.value, &replace63_1_1.value },
};
static const nir_search_expression replace63 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace63_0.value, &replace63_1.value },
};
   
static const nir_search_variable search64_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search64_0_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search64_0 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search64_0_0.value, &search64_0_1.value },
};

static const nir_search_variable search64_1_0 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable search64_1_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression search64_1 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search64_1_0.value, &search64_1_1.value },
};
static const nir_search_expression search64 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search64_0.value, &search64_1.value },
};
   
static const nir_search_variable replace64_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace64_0_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace64_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &replace64_0_0.value, &replace64_0_1.value },
};

static const nir_search_variable replace64_1 = {
   { nir_search_value_variable },
   1, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace64 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace64_0.value, &replace64_1.value },
};
   
static const nir_search_variable search87_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search87_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search87 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search87_0.value, &search87_1.value },
};
   
static const nir_search_variable replace87 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search88_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search88_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search88 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search88_0.value, &search88_1.value },
};
   
static const nir_search_variable replace88 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search93_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search93_0 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search93_0_0.value },
};

static const nir_search_variable search93_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search93_1 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &search93_1_0.value },
};
static const nir_search_expression search93 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &search93_0.value, &search93_1.value },
};
   
static const nir_search_variable replace93_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace93_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace93_0 = {
   { nir_search_value_expression },
   nir_op_iand,
   { &replace93_0_0.value, &replace93_0_1.value },
};
static const nir_search_expression replace93 = {
   { nir_search_value_expression },
   nir_op_inot,
   { &replace93_0.value },
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search61, &replace61.value, 0 },
   { &search62, &replace62.value, 0 },
   { &search63, &replace63.value, 0 },
   { &search64, &replace64.value, 0 },
   { &search87, &replace87.value, 0 },
   { &search88, &replace88.value, 0 },
   { &search93, &replace93.value, 0 },
};
   
static const nir_search_variable search139_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search139_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search139 = {
   { nir_search_value_expression },
   nir_op_ussub_4x8,
   { &search139_0.value, &search139_1.value },
};
   
static const nir_search_variable replace139 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search140_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search140_1 = {
   { nir_search_value_constant },
   { 0xffffffff /* -1 */ },
};
static const nir_search_expression search140 = {
   { nir_search_value_expression },
   nir_op_ussub_4x8,
   { &search140_0.value, &search140_1.value },
};
   
static const nir_search_constant replace140 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ussub_4x8_xforms[] = {
   { &search139, &replace139.value, 0 },
   { &search140, &replace140.value, 0 },
};
   
static const nir_search_variable search53_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search53_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search53 = {
   { nir_search_value_expression },
   nir_op_imax,
   { &search53_0.value, &search53_1.value },
};
   
static const nir_search_variable replace53 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_imax_xforms[] = {
   { &search53, &replace53.value, 0 },
};
   
static const nir_search_variable search58_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search58 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &search58_0.value },
};
   
static const nir_search_variable replace58_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace58_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace58_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &replace58_0_0.value, &replace58_0_1.value },
};

static const nir_search_constant replace58_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression replace58 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &replace58_0.value, &replace58_1.value },
};
   
static const nir_search_variable search59_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search59_0 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &search59_0_0.value },
};
static const nir_search_expression search59 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &search59_0.value },
};
   
static const nir_search_variable replace59_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace59 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &replace59_0.value },
};
   
static const nir_search_variable search73_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search73_0_0 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &search73_0_0_0.value },
};

static const nir_search_variable search73_0_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search73_0_1 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &search73_0_1_0.value },
};
static const nir_search_expression search73_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search73_0_0.value, &search73_0_1.value },
};
static const nir_search_expression search73 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &search73_0.value },
};
   
static const nir_search_variable replace73_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace73_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace73_0 = {
   { nir_search_value_expression },
   nir_op_ior,
   { &replace73_0_0.value, &replace73_0_1.value },
};
static const nir_search_expression replace73 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace73_0.value },
};

static const struct transform nir_opt_algebraic_fsat_xforms[] = {
   { &search58, &replace58.value, 7 },
   { &search59, &replace59.value, 0 },
   { &search73, &replace73.value, 0 },
};
   
static const nir_search_variable search45_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search45_0_0 = {
   { nir_search_value_expression },
   nir_op_fabs,
   { &search45_0_0_0.value },
};
static const nir_search_expression search45_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search45_0_0.value },
};

static const nir_search_constant search45_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search45 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search45_0.value, &search45_1.value },
};
   
static const nir_search_variable replace45_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace45_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace45 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace45_0.value, &replace45_1.value },
};
   
static const nir_search_variable search151_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search151_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search151_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search151_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search151_0_0.value, &search151_0_1.value, &search151_0_2.value },
};

static const nir_search_variable search151_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search151 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search151_0.value, &search151_1.value },
};
   
static const nir_search_variable replace151_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace151_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace151_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace151_1 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace151_1_0.value, &replace151_1_1.value },
};

static const nir_search_variable replace151_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace151_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace151_2 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace151_2_0.value, &replace151_2_1.value },
};
static const nir_search_expression replace151 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace151_0.value, &replace151_1.value, &replace151_2.value },
};
   
static const nir_search_variable search152_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search152_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search152_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search152_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search152_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search152_1_0.value, &search152_1_1.value, &search152_1_2.value },
};
static const nir_search_expression search152 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search152_0.value, &search152_1.value },
};
   
static const nir_search_variable replace152_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace152_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace152_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace152_1 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace152_1_0.value, &replace152_1_1.value },
};

static const nir_search_variable replace152_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace152_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace152_2 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace152_2_0.value, &replace152_2_1.value },
};
static const nir_search_expression replace152 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace152_0.value, &replace152_1.value, &replace152_2.value },
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search45, &replace45.value, 0 },
   { &search151, &replace151.value, 0 },
   { &search152, &replace152.value, 0 },
};
   
static const nir_search_variable search111_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search111_0 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search111_0_0.value },
};
static const nir_search_expression search111 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search111_0.value },
};
   
static const nir_search_variable replace111_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace111_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace111_0_0.value },
};
static const nir_search_expression replace111 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &replace111_0.value },
};
   
static const nir_search_variable search121_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search121_0 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search121_0_0.value },
};
static const nir_search_expression search121 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search121_0.value },
};
   
static const nir_search_variable replace121 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search122_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search122_0 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &search122_0_0.value },
};
static const nir_search_expression search122 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search122_0.value },
};
   
static const nir_search_variable replace122_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace122 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &replace122_0.value },
};
   
static const nir_search_variable search124_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search124_0 = {
   { nir_search_value_expression },
   nir_op_frsq,
   { &search124_0_0.value },
};
static const nir_search_expression search124 = {
   { nir_search_value_expression },
   nir_op_frcp,
   { &search124_0.value },
};
   
static const nir_search_variable replace124_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace124 = {
   { nir_search_value_expression },
   nir_op_fsqrt,
   { &replace124_0.value },
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search111, &replace111.value, 0 },
   { &search121, &replace121.value, 0 },
   { &search122, &replace122.value, 0 },
   { &search124, &replace124.value, 12 },
};
   
static const nir_search_variable search89_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search89_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search89 = {
   { nir_search_value_expression },
   nir_op_fxor,
   { &search89_0.value, &search89_1.value },
};
   
static const nir_search_constant replace89 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search89, &replace89.value, 0 },
};
   
static const nir_search_constant search98_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search98_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search98 = {
   { nir_search_value_expression },
   nir_op_ushr,
   { &search98_0.value, &search98_1.value },
};
   
static const nir_search_constant replace98 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search99_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search99_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search99 = {
   { nir_search_value_expression },
   nir_op_ushr,
   { &search99_0.value, &search99_1.value },
};
   
static const nir_search_variable replace99 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search98, &replace98.value, 0 },
   { &search99, &replace99.value, 0 },
};
   
static const nir_search_variable search100_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search100_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search100_0_0.value },
};
static const nir_search_expression search100 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search100_0.value },
};
   
static const nir_search_variable replace100 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search103_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search103_0_0 = {
   { nir_search_value_expression },
   nir_op_flog2,
   { &search103_0_0_0.value },
};

static const nir_search_variable search103_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search103_0 = {
   { nir_search_value_expression },
   nir_op_fmul,
   { &search103_0_0.value, &search103_0_1.value },
};
static const nir_search_expression search103 = {
   { nir_search_value_expression },
   nir_op_fexp2,
   { &search103_0.value },
};
   
static const nir_search_variable replace103_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace103_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace103 = {
   { nir_search_value_expression },
   nir_op_fpow,
   { &replace103_0.value, &replace103_1.value },
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search100, &replace100.value, 0 },
   { &search103, &replace103.value, 10 },
};
   
static const nir_search_constant search96_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search96_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search96 = {
   { nir_search_value_expression },
   nir_op_ishr,
   { &search96_0.value, &search96_1.value },
};
   
static const nir_search_constant replace96 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
   
static const nir_search_variable search97_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search97_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search97 = {
   { nir_search_value_expression },
   nir_op_ishr,
   { &search97_0.value, &search97_1.value },
};
   
static const nir_search_variable replace97 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search96, &replace96.value, 0 },
   { &search97, &replace97.value, 0 },
};
   
static const nir_search_variable search65_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search65_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search65 = {
   { nir_search_value_expression },
   nir_op_slt,
   { &search65_0.value, &search65_1.value },
};
   
static const nir_search_variable replace65_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace65_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace65_0 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace65_0_0.value, &replace65_0_1.value },
};
static const nir_search_expression replace65 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &replace65_0.value },
};

static const struct transform nir_opt_algebraic_slt_xforms[] = {
   { &search65, &replace65.value, 8 },
};
   
static const nir_search_variable search135_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search135_0 = {
   { nir_search_value_expression },
   nir_op_ftrunc,
   { &search135_0_0.value },
};
static const nir_search_expression search135 = {
   { nir_search_value_expression },
   nir_op_f2i,
   { &search135_0.value },
};
   
static const nir_search_variable replace135_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace135 = {
   { nir_search_value_expression },
   nir_op_f2i,
   { &replace135_0.value },
};

static const struct transform nir_opt_algebraic_f2i_xforms[] = {
   { &search135, &replace135.value, 0 },
};
   
static const nir_search_variable search75_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search75_0_0 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &search75_0_0_0.value },
};
static const nir_search_expression search75_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search75_0_0.value },
};

static const nir_search_constant search75_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search75 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search75_0.value, &search75_1.value },
};
   
static const nir_search_variable replace75 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_constant search76_0_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search76_0_1_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search76_0_1 = {
   { nir_search_value_expression },
   nir_op_b2f,
   { &search76_0_1_0.value },
};
static const nir_search_expression search76_0 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &search76_0_0.value, &search76_0_1.value },
};

static const nir_search_constant search76_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search76 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search76_0.value, &search76_1.value },
};
   
static const nir_search_variable replace76 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search149_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search149_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search149_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search149_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search149_0_0.value, &search149_0_1.value, &search149_0_2.value },
};

static const nir_search_variable search149_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search149 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search149_0.value, &search149_1.value },
};
   
static const nir_search_variable replace149_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace149_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace149_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace149_1 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace149_1_0.value, &replace149_1_1.value },
};

static const nir_search_variable replace149_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace149_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace149_2 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace149_2_0.value, &replace149_2_1.value },
};
static const nir_search_expression replace149 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace149_0.value, &replace149_1.value, &replace149_2.value },
};
   
static const nir_search_variable search150_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search150_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search150_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search150_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search150_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search150_1_0.value, &search150_1_1.value, &search150_1_2.value },
};
static const nir_search_expression search150 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search150_0.value, &search150_1.value },
};
   
static const nir_search_variable replace150_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace150_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace150_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace150_1 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace150_1_0.value, &replace150_1_1.value },
};

static const nir_search_variable replace150_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace150_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace150_2 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace150_2_0.value, &replace150_2_1.value },
};
static const nir_search_expression replace150 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace150_0.value, &replace150_1.value, &replace150_2.value },
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search75, &replace75.value, 0 },
   { &search76, &replace76.value, 0 },
   { &search149, &replace149.value, 0 },
   { &search150, &replace150.value, 0 },
};
   
static const nir_search_variable search81_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search81_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search81 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &search81_0.value, &search81_1.value },
};
   
static const nir_search_constant replace81 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search165_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search165_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search165_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search165_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search165_0_0.value, &search165_0_1.value, &search165_0_2.value },
};

static const nir_search_variable search165_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search165 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &search165_0.value, &search165_1.value },
};
   
static const nir_search_variable replace165_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace165_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace165_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace165_1 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace165_1_0.value, &replace165_1_1.value },
};

static const nir_search_variable replace165_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace165_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace165_2 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace165_2_0.value, &replace165_2_1.value },
};
static const nir_search_expression replace165 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace165_0.value, &replace165_1.value, &replace165_2.value },
};
   
static const nir_search_variable search166_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search166_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search166_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search166_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search166_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search166_1_0.value, &search166_1_1.value, &search166_1_2.value },
};
static const nir_search_expression search166 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &search166_0.value, &search166_1.value },
};
   
static const nir_search_variable replace166_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace166_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace166_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace166_1 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace166_1_0.value, &replace166_1_1.value },
};

static const nir_search_variable replace166_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace166_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace166_2 = {
   { nir_search_value_expression },
   nir_op_ult,
   { &replace166_2_0.value, &replace166_2_1.value },
};
static const nir_search_expression replace166 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace166_0.value, &replace166_1.value, &replace166_2.value },
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search81, &replace81.value, 0 },
   { &search165, &replace165.value, 0 },
   { &search166, &replace166.value, 0 },
};
   
static const nir_search_variable search137_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search137_1_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable search137_1_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search137_1 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &search137_1_0.value, &search137_1_1.value },
};
static const nir_search_expression search137 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &search137_0.value, &search137_1.value },
};
   
static const nir_search_variable replace137_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace137_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace137 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace137_0.value, &replace137_1.value },
};
   
static const nir_search_variable search141_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search141_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search141 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &search141_0.value, &search141_1.value },
};
   
static const nir_search_variable replace141_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace141_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace141_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace141_1_0.value },
};
static const nir_search_expression replace141 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &replace141_0.value, &replace141_1.value },
};

static const struct transform nir_opt_algebraic_fsub_xforms[] = {
   { &search137, &replace137.value, 0 },
   { &search141, &replace141.value, 13 },
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
   
static const nir_search_variable search143_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search143 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search143_0.value },
};
   
static const nir_search_constant replace143_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};

static const nir_search_variable replace143_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace143 = {
   { nir_search_value_expression },
   nir_op_fsub,
   { &replace143_0.value, &replace143_1.value },
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search0, &replace0.value, 0 },
   { &search143, &replace143.value, 14 },
};
   
static const nir_search_variable search69_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search69_0 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &search69_0_0.value },
};

static const nir_search_variable search69_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search69 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search69_0.value, &search69_1.value },
};
   
static const nir_search_variable replace69_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace69_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace69 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace69_0.value, &replace69_1.value },
};
   
static const nir_search_variable search155_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search155_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search155_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search155_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search155_0_0.value, &search155_0_1.value, &search155_0_2.value },
};

static const nir_search_variable search155_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search155 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search155_0.value, &search155_1.value },
};
   
static const nir_search_variable replace155_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace155_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace155_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace155_1 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace155_1_0.value, &replace155_1_1.value },
};

static const nir_search_variable replace155_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace155_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace155_2 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace155_2_0.value, &replace155_2_1.value },
};
static const nir_search_expression replace155 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace155_0.value, &replace155_1.value, &replace155_2.value },
};
   
static const nir_search_variable search156_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search156_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search156_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search156_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search156_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search156_1_0.value, &search156_1_1.value, &search156_1_2.value },
};
static const nir_search_expression search156 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search156_0.value, &search156_1.value },
};
   
static const nir_search_variable replace156_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace156_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace156_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace156_1 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace156_1_0.value, &replace156_1_1.value },
};

static const nir_search_variable replace156_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace156_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace156_2 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace156_2_0.value, &replace156_2_1.value },
};
static const nir_search_expression replace156 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace156_0.value, &replace156_1.value, &replace156_2.value },
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search69, &replace69.value, 0 },
   { &search155, &replace155.value, 0 },
   { &search156, &replace156.value, 0 },
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
   
static const nir_search_constant search148_0_0 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};

static const nir_search_variable search148_0_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search148_0 = {
   { nir_search_value_expression },
   nir_op_isub,
   { &search148_0_0.value, &search148_0_1.value },
};
static const nir_search_expression search148 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &search148_0.value },
};
   
static const nir_search_variable replace148_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace148 = {
   { nir_search_value_expression },
   nir_op_iabs,
   { &replace148_0.value },
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search4, &replace4.value, 0 },
   { &search5, &replace5.value, 0 },
   { &search148, &replace148.value, 0 },
};
   
static const nir_search_variable search50_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search50_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search50 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &search50_0.value, &search50_1.value },
};
   
static const nir_search_variable replace50 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search56_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search56_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search56_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &search56_0_0.value, &search56_0_1.value },
};

static const nir_search_constant search56_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search56 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &search56_0.value, &search56_1.value },
};
   
static const nir_search_variable replace56_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace56 = {
   { nir_search_value_expression },
   nir_op_fsat,
   { &replace56_0.value },
};
   
static const nir_search_variable search60_0_0_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant search60_0_0_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search60_0_0_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &search60_0_0_0_0.value, &search60_0_0_0_1.value },
};

static const nir_search_constant search60_0_0_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search60_0_0 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &search60_0_0_0.value, &search60_0_0_1.value },
};

static const nir_search_constant search60_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search60_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &search60_0_0.value, &search60_0_1.value },
};

static const nir_search_constant search60_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression search60 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &search60_0.value, &search60_1.value },
};
   
static const nir_search_variable replace60_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_constant replace60_0_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace60_0 = {
   { nir_search_value_expression },
   nir_op_fmax,
   { &replace60_0_0.value, &replace60_0_1.value },
};

static const nir_search_constant replace60_1 = {
   { nir_search_value_constant },
   { 0x3f800000 /* 1.0 */ },
};
static const nir_search_expression replace60 = {
   { nir_search_value_expression },
   nir_op_fmin,
   { &replace60_0.value, &replace60_1.value },
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search50, &replace50.value, 0 },
   { &search56, &replace56.value, 6 },
   { &search60, &replace60.value, 0 },
};
   
static const nir_search_variable search80_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search80_1 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search80 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search80_0.value, &search80_1.value },
};
   
static const nir_search_constant replace80 = {
   { nir_search_value_constant },
   { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search125_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_bool,
};

static const nir_search_constant search125_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0 */ },
};
static const nir_search_expression search125 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search125_0.value, &search125_1.value },
};
   
static const nir_search_variable replace125 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
   
static const nir_search_variable search163_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search163_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search163_0_2 = {
   { nir_search_value_variable },
   2, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search163_0 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search163_0_0.value, &search163_0_1.value, &search163_0_2.value },
};

static const nir_search_variable search163_1 = {
   { nir_search_value_variable },
   3, /* d */
   true,
   nir_type_invalid,
};
static const nir_search_expression search163 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search163_0.value, &search163_1.value },
};
   
static const nir_search_variable replace163_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace163_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace163_1_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace163_1 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace163_1_0.value, &replace163_1_1.value },
};

static const nir_search_variable replace163_2_0 = {
   { nir_search_value_variable },
   2, /* c */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace163_2_1 = {
   { nir_search_value_variable },
   3, /* d */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace163_2 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace163_2_0.value, &replace163_2_1.value },
};
static const nir_search_expression replace163 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace163_0.value, &replace163_1.value, &replace163_2.value },
};
   
static const nir_search_variable search164_0 = {
   { nir_search_value_variable },
   0, /* d */
   true,
   nir_type_invalid,
};

static const nir_search_variable search164_1_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search164_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   true,
   nir_type_invalid,
};

static const nir_search_variable search164_1_2 = {
   { nir_search_value_variable },
   3, /* c */
   true,
   nir_type_invalid,
};
static const nir_search_expression search164_1 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &search164_1_0.value, &search164_1_1.value, &search164_1_2.value },
};
static const nir_search_expression search164 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &search164_0.value, &search164_1.value },
};
   
static const nir_search_variable replace164_0 = {
   { nir_search_value_variable },
   1, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace164_1_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace164_1_1 = {
   { nir_search_value_variable },
   2, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace164_1 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace164_1_0.value, &replace164_1_1.value },
};

static const nir_search_variable replace164_2_0 = {
   { nir_search_value_variable },
   0, /* d */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace164_2_1 = {
   { nir_search_value_variable },
   3, /* c */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace164_2 = {
   { nir_search_value_expression },
   nir_op_ine,
   { &replace164_2_0.value, &replace164_2_1.value },
};
static const nir_search_expression replace164 = {
   { nir_search_value_expression },
   nir_op_bcsel,
   { &replace164_0.value, &replace164_1.value, &replace164_2.value },
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search80, &replace80.value, 0 },
   { &search125, &replace125.value, 0 },
   { &search163, &replace163.value, 0 },
   { &search164, &replace164.value, 0 },
};
   
static const nir_search_variable search134_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};
static const nir_search_expression search134_0 = {
   { nir_search_value_expression },
   nir_op_b2i,
   { &search134_0_0.value },
};
static const nir_search_expression search134 = {
   { nir_search_value_expression },
   nir_op_i2b,
   { &search134_0.value },
};
   
static const nir_search_variable replace134 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const struct transform nir_opt_algebraic_i2b_xforms[] = {
   { &search134, &replace134.value, 0 },
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
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ixor:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ixor_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ixor_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_seq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_seq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_seq_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ilt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ilt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ilt_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_imul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imul_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_uge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_uge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_uge_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_usadd_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_usadd_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_usadd_4x8_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fmul:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmul_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmul_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ffma:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ffma_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ffma_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_umin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umin_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_umax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umax_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_bcsel:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_bcsel_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_bcsel_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_sge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_sge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_sge_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fsqrt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fsqrt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fsqrt_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_iadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iadd_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fand:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fand_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fand_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fabs:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fabs_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fabs_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ieq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ieq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ieq_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_imin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imin_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_frsq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_frsq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_frsq_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ineg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ineg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ineg_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fpow:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fpow_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fpow_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ige:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ige_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ige_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fdiv:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fdiv_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fdiv_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ffract:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ffract_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ffract_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fadd:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fadd_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fadd_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ishl:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ishl_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ishl_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flog2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flog2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flog2_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_inot:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_inot_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_inot_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_sne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_sne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_sne_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_f2u:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_f2u_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_f2u_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fcsel:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fcsel_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fcsel_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_isub:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_isub_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_isub_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fmax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmax_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_umul_unorm_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_umul_unorm_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_umul_unorm_4x8_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_feq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_feq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_feq_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flrp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flrp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flrp_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ior:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ior_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ior_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ussub_4x8:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ussub_4x8_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ussub_4x8_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_imax:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_imax_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_imax_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fsat:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fsat_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fsat_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fge_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_frcp:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_frcp_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_frcp_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fxor:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fxor_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fxor_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ushr:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ushr_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ushr_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fexp2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fexp2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fexp2_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ishr:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ishr_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ishr_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_slt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_slt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_slt_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_f2i:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_f2i_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_f2i_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_flt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_flt_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ult:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ult_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ult_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fsub:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fsub_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fsub_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fneg:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fneg_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fneg_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fne_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_iabs:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_iabs_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_iabs_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fmin:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_fmin_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_fmin_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_ine:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_ine_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_ine_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_i2b:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_i2b_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_i2b_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
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
nir_opt_algebraic_impl(nir_function_impl *impl, const bool *condition_flags)
{
   struct opt_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.progress = false;
   state.condition_flags = condition_flags;

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
   bool condition_flags[15];
   const nir_shader_compiler_options *options = shader->options;

   condition_flags[0] = true;
   condition_flags[1] = options->lower_flrp;
   condition_flags[2] = options->lower_ffract;
   condition_flags[3] = !options->lower_flrp;
   condition_flags[4] = options->lower_ffma;
   condition_flags[5] = !options->lower_ffma;
   condition_flags[6] = !options->lower_fsat;
   condition_flags[7] = options->lower_fsat;
   condition_flags[8] = options->lower_scmp;
   condition_flags[9] = options->lower_fpow;
   condition_flags[10] = !options->lower_fpow;
   condition_flags[11] = options->lower_fsqrt;
   condition_flags[12] = !options->lower_fsqrt;
   condition_flags[13] = options->lower_sub;
   condition_flags[14] = options->lower_negate;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress |= nir_opt_algebraic_impl(overload->impl, condition_flags);
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

struct opt_state {
   void *mem_ctx;
   bool progress;
   const bool *condition_flags;
};

#endif

   
static const nir_search_variable search175_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search175_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search175 = {
   { nir_search_value_expression },
   nir_op_fdot4,
   { &search175_0.value, &search175_1.value },
};
   
static const nir_search_variable replace175_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace175_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace175 = {
   { nir_search_value_expression },
   nir_op_fdot_replicated4,
   { &replace175_0.value, &replace175_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot4_xforms[] = {
   { &search175, &replace175.value, 15 },
};
   
static const nir_search_variable search169_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search169_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search169_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search169_0_0.value, &search169_0_1.value },
};

static const nir_search_constant search169_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search169 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &search169_0.value, &search169_1.value },
};
   
static const nir_search_variable replace169_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace169_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace169_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace169_1_0.value },
};
static const nir_search_expression replace169 = {
   { nir_search_value_expression },
   nir_op_flt,
   { &replace169_0.value, &replace169_1.value },
};

static const struct transform nir_opt_algebraic_late_flt_xforms[] = {
   { &search169, &replace169.value, 0 },
};
   
static const nir_search_variable search173_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search173_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search173 = {
   { nir_search_value_expression },
   nir_op_fdot2,
   { &search173_0.value, &search173_1.value },
};
   
static const nir_search_variable replace173_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace173_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace173 = {
   { nir_search_value_expression },
   nir_op_fdot_replicated2,
   { &replace173_0.value, &replace173_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot2_xforms[] = {
   { &search173, &replace173.value, 15 },
};
   
static const nir_search_variable search174_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search174_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search174 = {
   { nir_search_value_expression },
   nir_op_fdot3,
   { &search174_0.value, &search174_1.value },
};
   
static const nir_search_variable replace174_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace174_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace174 = {
   { nir_search_value_expression },
   nir_op_fdot_replicated3,
   { &replace174_0.value, &replace174_1.value },
};

static const struct transform nir_opt_algebraic_late_fdot3_xforms[] = {
   { &search174, &replace174.value, 15 },
};
   
static const nir_search_variable search172_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search172_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search172_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search172_0_0.value, &search172_0_1.value },
};

static const nir_search_constant search172_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search172 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &search172_0.value, &search172_1.value },
};
   
static const nir_search_variable replace172_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace172_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace172_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace172_1_0.value },
};
static const nir_search_expression replace172 = {
   { nir_search_value_expression },
   nir_op_fne,
   { &replace172_0.value, &replace172_1.value },
};

static const struct transform nir_opt_algebraic_late_fne_xforms[] = {
   { &search172, &replace172.value, 0 },
};
   
static const nir_search_variable search171_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search171_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search171_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search171_0_0.value, &search171_0_1.value },
};

static const nir_search_constant search171_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search171 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &search171_0.value, &search171_1.value },
};
   
static const nir_search_variable replace171_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace171_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace171_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace171_1_0.value },
};
static const nir_search_expression replace171 = {
   { nir_search_value_expression },
   nir_op_feq,
   { &replace171_0.value, &replace171_1.value },
};

static const struct transform nir_opt_algebraic_late_feq_xforms[] = {
   { &search171, &replace171.value, 0 },
};
   
static const nir_search_variable search170_0_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search170_0_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search170_0 = {
   { nir_search_value_expression },
   nir_op_fadd,
   { &search170_0_0.value, &search170_0_1.value },
};

static const nir_search_constant search170_1 = {
   { nir_search_value_constant },
   { 0x0 /* 0.0 */ },
};
static const nir_search_expression search170 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &search170_0.value, &search170_1.value },
};
   
static const nir_search_variable replace170_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace170_1_0 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace170_1 = {
   { nir_search_value_expression },
   nir_op_fneg,
   { &replace170_1_0.value },
};
static const nir_search_expression replace170 = {
   { nir_search_value_expression },
   nir_op_fge,
   { &replace170_0.value, &replace170_1.value },
};

static const struct transform nir_opt_algebraic_late_fge_xforms[] = {
   { &search170, &replace170.value, 0 },
};
   
static const nir_search_variable search176_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable search176_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression search176 = {
   { nir_search_value_expression },
   nir_op_fdph,
   { &search176_0.value, &search176_1.value },
};
   
static const nir_search_variable replace176_0 = {
   { nir_search_value_variable },
   0, /* a */
   false,
   nir_type_invalid,
};

static const nir_search_variable replace176_1 = {
   { nir_search_value_variable },
   1, /* b */
   false,
   nir_type_invalid,
};
static const nir_search_expression replace176 = {
   { nir_search_value_expression },
   nir_op_fdph_replicated,
   { &replace176_0.value, &replace176_1.value },
};

static const struct transform nir_opt_algebraic_late_fdph_xforms[] = {
   { &search176, &replace176.value, 15 },
};

static bool
nir_opt_algebraic_late_block(nir_block *block, void *void_state)
{
   struct opt_state *state = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (!alu->dest.dest.is_ssa)
         continue;

      switch (alu->op) {
      case nir_op_fdot4:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdot4_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdot4_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_flt:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_flt_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_flt_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fdot2:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdot2_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdot2_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fdot3:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdot3_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdot3_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fne:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fne_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fne_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_feq:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_feq_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_feq_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fge:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fge_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fge_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
                                  state->mem_ctx)) {
               state->progress = true;
               break;
            }
         }
         break;
      case nir_op_fdph:
         for (unsigned i = 0; i < ARRAY_SIZE(nir_opt_algebraic_late_fdph_xforms); i++) {
            const struct transform *xform = &nir_opt_algebraic_late_fdph_xforms[i];
            if (state->condition_flags[xform->condition_offset] &&
                nir_replace_instr(alu, xform->search, xform->replace,
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
nir_opt_algebraic_late_impl(nir_function_impl *impl, const bool *condition_flags)
{
   struct opt_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.progress = false;
   state.condition_flags = condition_flags;

   nir_foreach_block(impl, nir_opt_algebraic_late_block, &state);

   if (state.progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return state.progress;
}


bool
nir_opt_algebraic_late(nir_shader *shader)
{
   bool progress = false;
   bool condition_flags[16];
   const nir_shader_compiler_options *options = shader->options;

   condition_flags[0] = true;
   condition_flags[1] = options->lower_flrp;
   condition_flags[2] = options->lower_ffract;
   condition_flags[3] = !options->lower_flrp;
   condition_flags[4] = options->lower_ffma;
   condition_flags[5] = !options->lower_ffma;
   condition_flags[6] = !options->lower_fsat;
   condition_flags[7] = options->lower_fsat;
   condition_flags[8] = options->lower_scmp;
   condition_flags[9] = options->lower_fpow;
   condition_flags[10] = !options->lower_fpow;
   condition_flags[11] = options->lower_fsqrt;
   condition_flags[12] = !options->lower_fsqrt;
   condition_flags[13] = options->lower_sub;
   condition_flags[14] = options->lower_negate;
   condition_flags[15] = options->fdot_replicates;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress |= nir_opt_algebraic_late_impl(overload->impl, condition_flags);
   }

   return progress;
}

