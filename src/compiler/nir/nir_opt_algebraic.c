
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

   
static const nir_search_variable search123_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search123_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search123 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
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
   nir_op_b2f,
   { &replace123_0.value },
   NULL,
};
   
static const nir_search_variable search134_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search134_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search134 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search134_0.value, &search134_1.value },
   NULL,
};
   
static const nir_search_variable replace134 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search135_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search135_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search135 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search135_0.value, &search135_1.value },
   NULL,
};
   
static const nir_search_variable replace135 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search136_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search136_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search136 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search136_0.value, &search136_1.value },
   NULL,
};
   
static const nir_search_constant replace136 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search144_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search144_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search144_0_0.value },
   NULL,
};

static const nir_search_variable search144_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search144_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search144_1_0.value },
   NULL,
};
static const nir_search_expression search144 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search144_0.value, &search144_1.value },
   NULL,
};
   
static const nir_search_variable replace144_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace144_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace144_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace144_0_0.value, &replace144_0_1.value },
   NULL,
};
static const nir_search_expression replace144 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace144_0.value },
   NULL,
};
   
static const nir_search_constant search152_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search152_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search152_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression search152_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search152_1_0.value, &search152_1_1.value },
   NULL,
};
static const nir_search_expression search152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
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

static const nir_search_constant replace152_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression replace152 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace152_0.value, &replace152_1.value },
   NULL,
};
   
static const nir_search_constant search153_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

static const nir_search_variable search153_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search153_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search153_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search153_1_0.value, &search153_1_1.value },
   NULL,
};
static const nir_search_expression search153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search153_0.value, &search153_1.value },
   NULL,
};
   
static const nir_search_variable replace153_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace153_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace153 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace153_0.value, &replace153_1.value },
   NULL,
};
   
static const nir_search_constant search205_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search205_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search205_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search205_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search205_1_0.value, &search205_1_1.value },
   NULL,
};
static const nir_search_expression search205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search205_0.value, &search205_1.value },
   NULL,
};
   
static const nir_search_variable replace205_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace205_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace205 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace205_0.value, &replace205_1.value },
   NULL,
};
   
static const nir_search_constant search206_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search206_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search206_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search206_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search206_1_0.value, &search206_1_1.value },
   NULL,
};
static const nir_search_expression search206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search206_0.value, &search206_1.value },
   NULL,
};
   
static const nir_search_variable replace206_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace206_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace206 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace206_0.value, &replace206_1.value },
   NULL,
};
   
static const nir_search_constant search207_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};

static const nir_search_variable search207_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search207_0.value, &search207_1.value },
   NULL,
};
   
static const nir_search_variable replace207_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace207_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace207 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace207_0.value, &replace207_1.value },
   NULL,
};
   
static const nir_search_constant search209_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};

static const nir_search_variable search209_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search209_0.value, &search209_1.value },
   NULL,
};
   
static const nir_search_variable replace209_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace209_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace209 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace209_0.value, &replace209_1.value },
   NULL,
};
   
static const nir_search_variable search251_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search251_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search251_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search251_0_0.value, &search251_0_1.value },
   NULL,
};

static const nir_search_variable search251_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search251_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search251_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search251_1_0.value, &search251_1_1.value },
   NULL,
};
static const nir_search_expression search251 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search251_0.value, &search251_1.value },
   NULL,
};
   
static const nir_search_variable replace251_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace251_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace251 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace251_0.value, &replace251_1.value },
   NULL,
};
   
static const nir_search_variable search252_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search252_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search252_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search252_0_0.value, &search252_0_1.value },
   NULL,
};

static const nir_search_variable search252_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search252_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search252_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search252_1_0.value, &search252_1_1.value },
   NULL,
};
static const nir_search_expression search252 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search252_0.value, &search252_1.value },
   NULL,
};
   
static const nir_search_variable replace252_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace252_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace252 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace252_0.value, &replace252_1.value },
   NULL,
};
   
static const nir_search_variable search253_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search253_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search253_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search253_0_0.value, &search253_0_1.value },
   NULL,
};

static const nir_search_variable search253_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search253_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search253_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search253_1_0.value, &search253_1_1.value },
   NULL,
};
static const nir_search_expression search253 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search253_0.value, &search253_1.value },
   NULL,
};
   
static const nir_search_variable replace253_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace253_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace253 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace253_0.value, &replace253_1.value },
   NULL,
};
   
static const nir_search_variable search254_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search254_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search254_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search254_0_0.value, &search254_0_1.value },
   NULL,
};

static const nir_search_variable search254_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search254_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search254_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search254_1_0.value, &search254_1_1.value },
   NULL,
};
static const nir_search_expression search254 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search254_0.value, &search254_1.value },
   NULL,
};
   
static const nir_search_variable replace254_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace254_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace254 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace254_0.value, &replace254_1.value },
   NULL,
};
   
static const nir_search_variable search255_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search255_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search255_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search255_0_0.value, &search255_0_1.value },
   NULL,
};

static const nir_search_variable search255_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search255_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search255_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search255_1_0.value, &search255_1_1.value },
   NULL,
};
static const nir_search_expression search255 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search255_0.value, &search255_1.value },
   NULL,
};
   
static const nir_search_variable replace255_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace255_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace255 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace255_0.value, &replace255_1.value },
   NULL,
};
   
static const nir_search_variable search256_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search256_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search256_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search256_0_0.value, &search256_0_1.value },
   NULL,
};

static const nir_search_variable search256_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search256_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search256_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search256_1_0.value, &search256_1_1.value },
   NULL,
};
static const nir_search_expression search256 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search256_0.value, &search256_1.value },
   NULL,
};
   
static const nir_search_variable replace256_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace256_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace256 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace256_0.value, &replace256_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iand_xforms[] = {
   { &search123, &replace123.value, 0 },
   { &search134, &replace134.value, 0 },
   { &search135, &replace135.value, 0 },
   { &search136, &replace136.value, 0 },
   { &search144, &replace144.value, 0 },
   { &search152, &replace152.value, 0 },
   { &search153, &replace153.value, 0 },
   { &search205, &replace205.value, 17 },
   { &search206, &replace206.value, 17 },
   { &search207, &replace207.value, 17 },
   { &search209, &replace209.value, 18 },
   { &search251, &replace251.value, 0 },
   { &search252, &replace252.value, 0 },
   { &search253, &replace253.value, 0 },
   { &search254, &replace254.value, 0 },
   { &search255, &replace255.value, 0 },
   { &search256, &replace256.value, 0 },
};
   
static const nir_search_variable search194_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search194_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search194_0_0.value },
   NULL,
};
static const nir_search_expression search194 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search194_0.value },
   NULL,
};
   
static const nir_search_variable replace194 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search197_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search197_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search197_0_0.value },
   NULL,
};
static const nir_search_expression search197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search197_0.value },
   NULL,
};
   
static const nir_search_variable replace197_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace197 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace197_0.value },
   NULL,
};
   
static const nir_search_variable search198_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search198_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search198_0_0.value },
   NULL,
};
static const nir_search_expression search198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &search198_0.value },
   NULL,
};
   
static const nir_search_variable replace198_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace198 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2b,
   { &replace198_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_i2b_xforms[] = {
   { &search194, &replace194.value, 0 },
   { &search197, &replace197.value, 0 },
   { &search198, &replace198.value, 0 },
};
   
static const nir_search_variable search141_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search141_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search141 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search141_0.value, &search141_1.value },
   NULL,
};
   
static const nir_search_constant replace141 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search142_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search142_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search142 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ixor,
   { &search142_0.value, &search142_1.value },
   NULL,
};
   
static const nir_search_variable replace142 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ixor_xforms[] = {
   { &search141, &replace141.value, 0 },
   { &search142, &replace142.value, 0 },
};
   
static const nir_search_variable search116_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search116_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search116 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
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

static const nir_search_variable replace116_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace116_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace116_0_0.value, &replace116_0_1.value },
   NULL,
};
static const nir_search_expression replace116 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace116_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_seq_xforms[] = {
   { &search116, &replace116.value, 11 },
};
   
static const nir_search_variable search127_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search127_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search127 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search127_0.value, &search127_1.value },
   NULL,
};
   
static const nir_search_constant replace127 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search265_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search265_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search265_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search265_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search265_0_0.value, &search265_0_1.value, &search265_0_2.value },
   NULL,
};

static const nir_search_variable search265_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search265 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search265_0.value, &search265_1.value },
   NULL,
};
   
static const nir_search_variable replace265_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace265_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace265_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace265_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace265_1_0.value, &replace265_1_1.value },
   NULL,
};

static const nir_search_variable replace265_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace265_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace265_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace265_2_0.value, &replace265_2_1.value },
   NULL,
};
static const nir_search_expression replace265 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace265_0.value, &replace265_1.value, &replace265_2.value },
   NULL,
};
   
static const nir_search_variable search266_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search266_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search266_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search266_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search266_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search266_1_0.value, &search266_1_1.value, &search266_1_2.value },
   NULL,
};
static const nir_search_expression search266 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search266_0.value, &search266_1.value },
   NULL,
};
   
static const nir_search_variable replace266_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace266_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace266_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace266_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace266_1_0.value, &replace266_1_1.value },
   NULL,
};

static const nir_search_variable replace266_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace266_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace266_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace266_2_0.value, &replace266_2_1.value },
   NULL,
};
static const nir_search_expression replace266 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace266_0.value, &replace266_1.value, &replace266_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ilt_xforms[] = {
   { &search127, &replace127.value, 0 },
   { &search265, &replace265.value, 0 },
   { &search266, &replace266.value, 0 },
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
   
static const nir_search_variable search120_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search120_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search120_0_0.value },
   NULL,
};

static const nir_search_variable search120_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search120_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search120_1_0.value },
   NULL,
};
static const nir_search_expression search120 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search120_0.value, &search120_1.value },
   NULL,
};
   
static const nir_search_variable replace120_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace120_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace120_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace120_0_0.value, &replace120_0_1.value },
   NULL,
};
static const nir_search_expression replace120 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace120_0.value },
   NULL,
};
   
static const nir_search_variable search223_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search223_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search223_0_0.value },
   NULL,
};

static const nir_search_variable search223_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search223_0.value, &search223_1.value },
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
   nir_op_imul,
   { &replace223_0_0.value, &replace223_0_1.value },
   NULL,
};
static const nir_search_expression replace223 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace223_0.value },
   NULL,
};
   
static const nir_search_variable search225_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search225_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search225_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search225_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search225_1_0.value, &search225_1_1.value },
   NULL,
};
static const nir_search_expression search225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search225_0.value, &search225_1.value },
   NULL,
};
   
static const nir_search_variable replace225_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace225_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace225_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace225_0_0.value, &replace225_0_1.value },
   NULL,
};

static const nir_search_variable replace225_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace225 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace225_0.value, &replace225_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imul_xforms[] = {
   { &search0, &replace0.value, 0 },
   { &search1, &replace1.value, 0 },
   { &search30, &replace30.value, 0 },
   { &search34, &replace34.value, 0 },
   { &search36, &replace36.value, 0 },
   { &search120, &replace120.value, 0 },
   { &search223, &replace223.value, 0 },
   { &search225, &replace225.value, 0 },
};
   
static const nir_search_variable search132_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search132_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search132 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search132_0.value, &search132_1.value },
   NULL,
};
   
static const nir_search_constant replace132 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search275_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search275_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search275_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search275_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search275_0_0.value, &search275_0_1.value, &search275_0_2.value },
   NULL,
};

static const nir_search_variable search275_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search275 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search275_0.value, &search275_1.value },
   NULL,
};
   
static const nir_search_variable replace275_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace275_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace275_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace275_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace275_1_0.value, &replace275_1_1.value },
   NULL,
};

static const nir_search_variable replace275_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace275_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace275_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace275_2_0.value, &replace275_2_1.value },
   NULL,
};
static const nir_search_expression replace275 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace275_0.value, &replace275_1.value, &replace275_2.value },
   NULL,
};
   
static const nir_search_variable search276_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search276_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search276_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search276_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search276_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search276_1_0.value, &search276_1_1.value, &search276_1_2.value },
   NULL,
};
static const nir_search_expression search276 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &search276_0.value, &search276_1.value },
   NULL,
};
   
static const nir_search_variable replace276_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace276_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace276_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace276_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace276_1_0.value, &replace276_1_1.value },
   NULL,
};

static const nir_search_variable replace276_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace276_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace276_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_uge,
   { &replace276_2_0.value, &replace276_2_1.value },
   NULL,
};
static const nir_search_expression replace276 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace276_0.value, &replace276_1.value, &replace276_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_uge_xforms[] = {
   { &search132, &replace132.value, 0 },
   { &search275, &replace275.value, 0 },
   { &search276, &replace276.value, 0 },
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
   
static const nir_search_variable search124_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search124_0 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_b2i,
   { &search124_0_0.value },
   NULL,
};
static const nir_search_expression search124 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search124_0.value },
   NULL,
};
   
static const nir_search_variable replace124 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search217_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search217_0.value },
   NULL,
};
   
static const nir_search_constant replace217_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace217_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace217 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace217_0.value, &replace217_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ineg_xforms[] = {
   { &search11, &replace11.value, 0 },
   { &search124, &replace124.value, 0 },
   { &search217, &replace217.value, 20 },
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
   
static const nir_search_variable search121_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search121_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search121_0_0.value },
   NULL,
};

static const nir_search_variable search121_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search121_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search121_1_0.value },
   NULL,
};
static const nir_search_expression search121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search121_0.value, &search121_1.value },
   NULL,
};
   
static const nir_search_variable replace121_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace121_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace121_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace121_0_0.value, &replace121_0_1.value },
   NULL,
};
static const nir_search_expression replace121 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace121_0.value },
   NULL,
};
   
static const nir_search_variable search172_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search172_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search172_0_0.value },
   NULL,
};

static const nir_search_variable search172_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search172_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search172_1_0.value },
   NULL,
};
static const nir_search_expression search172 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search172_0.value, &search172_1.value },
   NULL,
};
   
static const nir_search_variable replace172_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace172_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace172_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace172_0_0.value, &replace172_0_1.value },
   NULL,
};
static const nir_search_expression replace172 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace172_0.value },
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
   nir_op_fneg,
   { &search222_0_0.value },
   NULL,
};

static const nir_search_variable search222_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
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
   nir_op_fmul,
   { &replace222_0_0.value, &replace222_0_1.value },
   NULL,
};
static const nir_search_expression replace222 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace222_0.value },
   NULL,
};
   
static const nir_search_variable search224_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search224_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search224_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search224_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search224_1_0.value, &search224_1_1.value },
   NULL,
};
static const nir_search_expression search224 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &search224_0.value, &search224_1.value },
   NULL,
};
   
static const nir_search_variable replace224_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace224_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace224_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace224_0_0.value, &replace224_0_1.value },
   NULL,
};

static const nir_search_variable replace224_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace224 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace224_0.value, &replace224_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmul_xforms[] = {
   { &search29, &replace29.value, 0 },
   { &search33, &replace33.value, 0 },
   { &search35, &replace35.value, 0 },
   { &search121, &replace121.value, 0 },
   { &search172, &replace172.value, 0 },
   { &search222, &replace222.value, 0 },
   { &search224, &replace224.value, 0 },
};
   
static const nir_search_variable search201_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search201_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search201_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &search201_0_0.value, &search201_0_1.value },
   NULL,
};
static const nir_search_expression search201 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_x,
   { &search201_0.value },
   NULL,
};
   
static const nir_search_variable replace201 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_64_2x32_split_x_xforms[] = {
   { &search201, &replace201.value, 0 },
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
   
static const nir_search_variable search82_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   false,
   nir_op_umin,
   { &search82_0.value, &search82_1.value },
   NULL,
};
   
static const nir_search_variable replace82 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search102_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search102_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search102_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search102_0_0_0_0.value, &search102_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search102_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search102_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search102_0_0_0.value, &search102_0_0_1.value },
   NULL,
};

static const nir_search_variable search102_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search102_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &search102_0_0.value, &search102_0_1.value },
   NULL,
};

static const nir_search_variable search102_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &search102_0.value, &search102_1.value },
   NULL,
};
   
static const nir_search_variable replace102_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace102_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace102_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umax,
   { &replace102_0_0.value, &replace102_0_1.value },
   NULL,
};

static const nir_search_variable replace102_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace102 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_umin,
   { &replace102_0.value, &replace102_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_umin_xforms[] = {
   { &search82, &replace82.value, 0 },
   { &search102, &replace102.value, 0 },
};
   
static const nir_search_variable search83_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   nir_op_umax,
   { &search83_0.value, &search83_1.value },
   NULL,
};
   
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
   
static const nir_search_variable search73_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search73_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search73_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search73_0_0.value, &search73_0_1.value },
   NULL,
};

static const nir_search_variable search73_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search73_2 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search73_0.value, &search73_1.value, &search73_2.value },
   NULL,
};
   
static const nir_search_variable replace73_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace73_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace73 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
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
   nir_op_flt,
   { &search74_0_0.value, &search74_0_1.value },
   NULL,
};

static const nir_search_variable search74_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search74_2 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search74 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search74_0.value, &search74_1.value, &search74_2.value },
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
   nir_op_fmax,
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
static const nir_search_expression search75_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search75_0_0.value },
   NULL,
};

static const nir_search_variable search75_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search75_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search75_0.value, &search75_1.value, &search75_2.value },
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
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace75_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace75 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace75_0.value, &replace75_1.value, &replace75_2.value },
   NULL,
};
   
static const nir_search_variable search76_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search76_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search76_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search76_1_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search76_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search76_1_0.value, &search76_1_1.value, &search76_1_2.value },
   NULL,
};

static const nir_search_variable search76_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search76_0.value, &search76_1.value, &search76_2.value },
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

static const nir_search_variable replace76_2 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace76 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace76_0.value, &replace76_1.value, &replace76_2.value },
   NULL,
};
   
static const nir_search_variable search77_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search77_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_variable search77_2 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_bool32,
   NULL,
};
static const nir_search_expression search77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search77_0.value, &search77_1.value, &search77_2.value },
   NULL,
};
   
static const nir_search_variable replace77_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace77_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace77 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace77_0.value, &replace77_1.value },
   NULL,
};
   
static const nir_search_variable search183_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search183_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_constant search183_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search183 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search183_0.value, &search183_1.value, &search183_2.value },
   NULL,
};
   
static const nir_search_variable replace183 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search184_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search184_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

static const nir_search_constant search184_2 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search184_0.value, &search184_1.value, &search184_2.value },
   NULL,
};
   
static const nir_search_variable replace184_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace184 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace184_0.value },
   NULL,
};
   
static const nir_search_variable search185_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search185_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant search185_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search185 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search185_0.value, &search185_1.value, &search185_2.value },
   NULL,
};
   
static const nir_search_variable replace185_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace185 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace185_0.value },
   NULL,
};
   
static const nir_search_variable search186_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search186_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant search186_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search186 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search186_0.value, &search186_1.value, &search186_2.value },
   NULL,
};
   
static const nir_search_variable replace186_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace186_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace186_0_0.value },
   NULL,
};
static const nir_search_expression replace186 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace186_0.value },
   NULL,
};
   
static const nir_search_variable search187_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search187_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_constant search187_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};
static const nir_search_expression search187 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search187_0.value, &search187_1.value, &search187_2.value },
   NULL,
};
   
static const nir_search_variable replace187_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace187_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace187_0_0.value },
   NULL,
};
static const nir_search_expression replace187 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace187_0.value },
   NULL,
};
   
static const nir_search_variable search188_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search188_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};

static const nir_search_constant search188_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
static const nir_search_expression search188 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_bcsel,
   { &search188_0.value, &search188_1.value, &search188_2.value },
   NULL,
};
   
static const nir_search_variable replace188_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace188_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace188_0_0_0.value },
   NULL,
};
static const nir_search_expression replace188_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace188_0_0.value },
   NULL,
};
static const nir_search_expression replace188 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace188_0.value },
   NULL,
};
   
static const nir_search_constant search189_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};

static const nir_search_variable search189_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search189_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search189 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search189_0.value, &search189_1.value, &search189_2.value },
   NULL,
};
   
static const nir_search_variable replace189 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_constant search190_0 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};

static const nir_search_variable search190_1 = {
   { nir_search_value_variable, 0 },
   0, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search190_2 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search190 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search190_0.value, &search190_1.value, &search190_2.value },
   NULL,
};
   
static const nir_search_variable replace190 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search191_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search191_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search191_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search191_0.value, &search191_1.value, &search191_2.value },
   NULL,
};
   
static const nir_search_variable replace191_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace191_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace191_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace191_0_0.value, &replace191_0_1.value },
   NULL,
};

static const nir_search_variable replace191_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace191_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace191 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace191_0.value, &replace191_1.value, &replace191_2.value },
   NULL,
};
   
static const nir_search_variable search192_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search192_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search192_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search192 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search192_0.value, &search192_1.value, &search192_2.value },
   NULL,
};
   
static const nir_search_variable replace192 = {
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
   { &search183, &replace183.value, 0 },
   { &search184, &replace184.value, 0 },
   { &search185, &replace185.value, 0 },
   { &search186, &replace186.value, 0 },
   { &search187, &replace187.value, 0 },
   { &search188, &replace188.value, 0 },
   { &search189, &replace189.value, 0 },
   { &search190, &replace190.value, 0 },
   { &search191, &replace191.value, 0 },
   { &search192, &replace192.value, 0 },
};
   
static const nir_search_variable search115_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search115_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
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

static const nir_search_variable replace115_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace115_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace115_0_0.value, &replace115_0_1.value },
   NULL,
};
static const nir_search_expression replace115 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace115_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_sge_xforms[] = {
   { &search115, &replace115.value, 11 },
};
   
static const nir_search_variable search165_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search165_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search165_0_0.value },
   NULL,
};
static const nir_search_expression search165 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsqrt,
   { &search165_0.value },
   NULL,
};
   
static const nir_search_constant replace165_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

static const nir_search_variable replace165_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace165_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace165_0_0.value, &replace165_0_1.value },
   NULL,
};
static const nir_search_expression replace165 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace165_0.value },
   NULL,
};
   
static const nir_search_variable search177_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search177_0.value },
   NULL,
};
   
static const nir_search_variable replace177_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace177_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace177_0_0.value },
   NULL,
};
static const nir_search_expression replace177 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace177_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsqrt_xforms[] = {
   { &search165, &replace165.value, 0 },
   { &search177, &replace177.value, 15 },
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
   
static const nir_search_variable search219_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search219_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search219_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search219_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search219_1_0.value, &search219_1_1.value },
   NULL,
};
static const nir_search_expression search219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
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

static const nir_search_variable replace219_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace219 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace219_0.value, &replace219_1.value },
   NULL,
};
   
static const nir_search_variable search227_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search227_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search227_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search227_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search227_1_0.value, &search227_1_1.value },
   NULL,
};
static const nir_search_expression search227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search227_0.value, &search227_1.value },
   NULL,
};
   
static const nir_search_variable replace227_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace227_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace227_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace227_0_0.value, &replace227_0_1.value },
   NULL,
};

static const nir_search_variable replace227_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace227 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace227_0.value, &replace227_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iadd_xforms[] = {
   { &search18, &replace18.value, 0 },
   { &search22, &replace22.value, 0 },
   { &search24, &replace24.value, 0 },
   { &search25, &replace25.value, 0 },
   { &search26, &replace26.value, 0 },
   { &search219, &replace219.value, 0 },
   { &search227, &replace227.value, 0 },
};
   
static const nir_search_variable search240_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_2x16,
   { &search240_0.value },
   NULL,
};
   
static const nir_search_variable replace240_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace240_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace240_0_0_0_0_0.value },
   NULL,
};

static const nir_search_constant replace240_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
static const nir_search_expression replace240_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace240_0_0_0_0.value, &replace240_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace240_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace240_0_0_0.value },
   NULL,
};
static const nir_search_expression replace240_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &replace240_0_0.value },
   NULL,
};
static const nir_search_expression replace240 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace240_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_unorm_2x16_xforms[] = {
   { &search240, &replace240.value, 29 },
};
   
static const nir_search_variable search241_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_unorm_4x8,
   { &search241_0.value },
   NULL,
};
   
static const nir_search_variable replace241_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace241_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace241_0_0_0_0_0.value },
   NULL,
};

static const nir_search_constant replace241_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
static const nir_search_expression replace241_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace241_0_0_0_0.value, &replace241_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace241_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace241_0_0_0.value },
   NULL,
};
static const nir_search_expression replace241_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &replace241_0_0.value },
   NULL,
};
static const nir_search_expression replace241 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace241_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_unorm_4x8_xforms[] = {
   { &search241, &replace241.value, 30 },
};
   
static const nir_search_variable search243_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_4x8,
   { &search243_0.value },
   NULL,
};
   
static const nir_search_constant replace243_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace243_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace243_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace243_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace243_0_0_0_0_1_0.value, &replace243_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace243_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace243_0_0_0_0_0.value, &replace243_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace243_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
static const nir_search_expression replace243_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace243_0_0_0_0.value, &replace243_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace243_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace243_0_0_0.value },
   NULL,
};
static const nir_search_expression replace243_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &replace243_0_0.value },
   NULL,
};
static const nir_search_expression replace243 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec4_to_uint,
   { &replace243_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_snorm_4x8_xforms[] = {
   { &search243, &replace243.value, 32 },
};
   
static const nir_search_variable search133_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search133_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search133 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fand,
   { &search133_0.value, &search133_1.value },
   NULL,
};
   
static const nir_search_constant replace133 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fand_xforms[] = {
   { &search133, &replace133.value, 0 },
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
   
static const nir_search_variable search110_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search110_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search110_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &search110_0_0.value, &search110_0_1.value },
   NULL,
};
static const nir_search_expression search110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search110_0.value },
   NULL,
};
   
static const nir_search_variable replace110_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace110_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace110 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
   { &replace110_0.value, &replace110_1.value },
   NULL,
};
   
static const nir_search_variable search111_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search111_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search111_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &search111_0_0.value, &search111_0_1.value },
   NULL,
};
static const nir_search_expression search111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search111_0.value },
   NULL,
};
   
static const nir_search_variable replace111_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace111_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace111 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sge,
   { &replace111_0.value, &replace111_1.value },
   NULL,
};
   
static const nir_search_variable search112_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search112_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search112_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &search112_0_0.value, &search112_0_1.value },
   NULL,
};
static const nir_search_expression search112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search112_0.value },
   NULL,
};
   
static const nir_search_variable replace112_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace112_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace112 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_seq,
   { &replace112_0.value, &replace112_1.value },
   NULL,
};
   
static const nir_search_variable search113_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search113_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search113_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &search113_0_0.value, &search113_0_1.value },
   NULL,
};
static const nir_search_expression search113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search113_0.value },
   NULL,
};
   
static const nir_search_variable replace113_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace113_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace113 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
   { &replace113_0.value, &replace113_1.value },
   NULL,
};
   
static const nir_search_variable search199_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search199_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search199_0_0.value },
   NULL,
};
static const nir_search_expression search199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search199_0.value },
   NULL,
};
   
static const nir_search_variable replace199_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace199 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace199_0.value },
   NULL,
};
   
static const nir_search_constant search220_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search220_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search220_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search220_0_0.value, &search220_0_1.value },
   NULL,
};
static const nir_search_expression search220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search220_0.value },
   NULL,
};
   
static const nir_search_variable replace220_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace220 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace220_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fabs_xforms[] = {
   { &search12, &replace12.value, 0 },
   { &search13, &replace13.value, 0 },
   { &search14, &replace14.value, 0 },
   { &search110, &replace110.value, 0 },
   { &search111, &replace111.value, 0 },
   { &search112, &replace112.value, 0 },
   { &search113, &replace113.value, 0 },
   { &search199, &replace199.value, 0 },
   { &search220, &replace220.value, 0 },
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
   
static const nir_search_variable search129_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search129_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search129 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search129_0.value, &search129_1.value },
   NULL,
};
   
static const nir_search_constant replace129 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search179_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search179_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search179 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search179_0.value, &search179_1.value },
   NULL,
};
   
static const nir_search_variable replace179 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search182_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search182_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search182_0.value, &search182_1.value },
   (is_not_used_by_if),
};
   
static const nir_search_variable replace182_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace182 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace182_0.value },
   NULL,
};
   
static const nir_search_variable search269_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search269_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search269_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search269_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search269_0_0.value, &search269_0_1.value, &search269_0_2.value },
   NULL,
};

static const nir_search_variable search269_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search269 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
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

static const nir_search_variable replace269_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace269_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace269_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace269_1_0.value, &replace269_1_1.value },
   NULL,
};

static const nir_search_variable replace269_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace269_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace269_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace269_2_0.value, &replace269_2_1.value },
   NULL,
};
static const nir_search_expression replace269 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace269_0.value, &replace269_1.value, &replace269_2.value },
   NULL,
};
   
static const nir_search_variable search270_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search270_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search270_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search270_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search270_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search270_1_0.value, &search270_1_1.value, &search270_1_2.value },
   NULL,
};
static const nir_search_expression search270 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &search270_0.value, &search270_1.value },
   NULL,
};
   
static const nir_search_variable replace270_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace270_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace270_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace270_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace270_1_0.value, &replace270_1_1.value },
   NULL,
};

static const nir_search_variable replace270_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace270_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace270_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ieq,
   { &replace270_2_0.value, &replace270_2_1.value },
   NULL,
};
static const nir_search_expression replace270 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace270_0.value, &replace270_1.value, &replace270_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ieq_xforms[] = {
   { &search129, &replace129.value, 0 },
   { &search179, &replace179.value, 0 },
   { &search182, &replace182.value, 0 },
   { &search269, &replace269.value, 0 },
   { &search270, &replace270.value, 0 },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search232 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_usub_borrow,
   { &search232_0.value, &search232_1.value },
   NULL,
};
   
static const nir_search_variable replace232_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace232_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace232_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace232_0_0.value, &replace232_0_1.value },
   NULL,
};
static const nir_search_expression replace232 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace232_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_usub_borrow_xforms[] = {
   { &search232, &replace232.value, 24 },
};
   
static const nir_search_variable search80_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   false,
   nir_op_imin,
   { &search80_0.value, &search80_1.value },
   NULL,
};
   
static const nir_search_variable replace80 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search85_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search85_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search85_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search85_1_0.value },
   NULL,
};
static const nir_search_expression search85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
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
   nir_op_iabs,
   { &replace85_0_0.value },
   NULL,
};
static const nir_search_expression replace85 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace85_0.value },
   NULL,
};
   
static const nir_search_variable search87_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search87_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search87_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search87_1_0_0.value },
   NULL,
};
static const nir_search_expression search87_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search87_1_0.value },
   NULL,
};
static const nir_search_expression search87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search87_0.value, &search87_1.value },
   NULL,
};
   
static const nir_search_variable replace87_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace87_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace87_0_0.value },
   NULL,
};
static const nir_search_expression replace87 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace87_0.value },
   NULL,
};
   
static const nir_search_variable search89_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search89_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search89_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search89_1_0.value },
   NULL,
};
static const nir_search_expression search89 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search89_0.value, &search89_1.value },
   NULL,
};
   
static const nir_search_variable replace89 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search101_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search101_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search101_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search101_0_0_0_0.value, &search101_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search101_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search101_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search101_0_0_0.value, &search101_0_0_1.value },
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
   nir_op_imax,
   { &search101_0_0.value, &search101_0_1.value },
   NULL,
};

static const nir_search_variable search101_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search101_0.value, &search101_1.value },
   NULL,
};
   
static const nir_search_variable replace101_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace101_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace101_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace101_0_0.value, &replace101_0_1.value },
   NULL,
};

static const nir_search_variable replace101_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace101 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace101_0.value, &replace101_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imin_xforms[] = {
   { &search80, &replace80.value, 0 },
   { &search85, &replace85.value, 0 },
   { &search87, &replace87.value, 0 },
   { &search89, &replace89.value, 0 },
   { &search101, &replace101.value, 0 },
};
   
static const nir_search_variable search167_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search167_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search167_0_0.value },
   NULL,
};
static const nir_search_expression search167 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frsq,
   { &search167_0.value },
   NULL,
};
   
static const nir_search_constant replace167_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

static const nir_search_variable replace167_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace167_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace167_0_0.value, &replace167_0_1.value },
   NULL,
};
static const nir_search_expression replace167 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace167_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frsq_xforms[] = {
   { &search167, &replace167.value, 0 },
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
   
static const nir_search_variable search105_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search105_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search105_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search105_0_0_0.value, &search105_0_0_1.value },
   NULL,
};

static const nir_search_constant search105_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression search105_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &search105_0_0.value, &search105_0_1.value },
   NULL,
};

static const nir_search_constant search105_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search105_0.value, &search105_1.value },
   NULL,
};
   
static const nir_search_variable replace105_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace105_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace105_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace105_0_0.value, &replace105_0_1.value },
   NULL,
};

static const nir_search_constant replace105_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression replace105 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace105_0.value, &replace105_1.value },
   NULL,
};
   
static const nir_search_variable search237_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search237_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &search237_0.value, &search237_1.value },
   NULL,
};
   
static const nir_search_variable replace237_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace237_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace237_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression replace237_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace237_0_1_0.value, &replace237_0_1_1.value },
   NULL,
};
static const nir_search_expression replace237_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace237_0_0.value, &replace237_0_1.value },
   NULL,
};

static const nir_search_constant replace237_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff /* 255 */ },
};
static const nir_search_expression replace237 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace237_0.value, &replace237_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_u8_xforms[] = {
   { &search105, &replace105.value, 0 },
   { &search237, &replace237.value, 27 },
};
   
static const nir_search_variable search242_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_snorm_2x16,
   { &search242_0.value },
   NULL,
};
   
static const nir_search_constant replace242_0_0_0_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace242_0_0_0_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace242_0_0_0_0_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace242_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace242_0_0_0_0_1_0.value, &replace242_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace242_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace242_0_0_0_0_0.value, &replace242_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace242_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
static const nir_search_expression replace242_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace242_0_0_0_0.value, &replace242_0_0_0_1.value },
   NULL,
};
static const nir_search_expression replace242_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fround_even,
   { &replace242_0_0_0.value },
   NULL,
};
static const nir_search_expression replace242_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &replace242_0_0.value },
   NULL,
};
static const nir_search_expression replace242 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_uvec2_to_uint,
   { &replace242_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_pack_snorm_2x16_xforms[] = {
   { &search242, &replace242.value, 31 },
};
   
static const nir_search_variable search156_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search156_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search156_0.value, &search156_1.value },
   NULL,
};
   
static const nir_search_variable replace156_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace156_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace156_0_0_0.value },
   NULL,
};

static const nir_search_variable replace156_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace156_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace156_0_0.value, &replace156_0_1.value },
   NULL,
};
static const nir_search_expression replace156 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace156_0.value },
   NULL,
};
   
static const nir_search_variable search159_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search159_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search159 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search159_0.value, &search159_1.value },
   NULL,
};
   
static const nir_search_variable replace159 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search160_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search160_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};
static const nir_search_expression search160 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
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

static const nir_search_variable replace160_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace160 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace160_0.value, &replace160_1.value },
   NULL,
};
   
static const nir_search_variable search161_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search161_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4010000000000000 /* 4.0 */ },
};
static const nir_search_expression search161 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
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
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace161_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace161_0_0.value, &replace161_0_1.value },
   NULL,
};

static const nir_search_variable replace161_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace161_1_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace161_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace161_1_0.value, &replace161_1_1.value },
   NULL,
};
static const nir_search_expression replace161 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace161_0.value, &replace161_1.value },
   NULL,
};
   
static const nir_search_constant search162_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x4000000000000000 /* 2.0 */ },
};

static const nir_search_variable search162_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search162 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
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
static const nir_search_expression replace162 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace162_0.value },
   NULL,
};
   
static const nir_search_variable search163_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search163_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
static const nir_search_expression search163_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search163_0_0.value, &search163_0_1.value },
   NULL,
};

static const nir_search_constant search163_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
static const nir_search_expression search163 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
   { &search163_0.value, &search163_1.value },
   NULL,
};
   
static const nir_search_variable replace163 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search164_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search164_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x400199999999999a /* 2.2 */ },
};
static const nir_search_expression search164_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search164_0_0_0.value, &search164_0_0_1.value },
   NULL,
};
static const nir_search_expression search164_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search164_0_0.value },
   NULL,
};

static const nir_search_constant search164_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fdd1743e963dc48 /* 0.454545 */ },
};
static const nir_search_expression search164 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fpow,
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
static const nir_search_expression replace164 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace164_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fpow_xforms[] = {
   { &search156, &replace156.value, 12 },
   { &search159, &replace159.value, 0 },
   { &search160, &replace160.value, 0 },
   { &search161, &replace161.value, 0 },
   { &search162, &replace162.value, 0 },
   { &search163, &replace163.value, 0 },
   { &search164, &replace164.value, 0 },
};
   
static const nir_search_variable search128_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search128_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search128 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search128_0.value, &search128_1.value },
   NULL,
};
   
static const nir_search_constant replace128 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search267_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search267_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search267_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search267_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search267_0_0.value, &search267_0_1.value, &search267_0_2.value },
   NULL,
};

static const nir_search_variable search267_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search267 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
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

static const nir_search_variable replace267_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace267_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace267_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace267_1_0.value, &replace267_1_1.value },
   NULL,
};

static const nir_search_variable replace267_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace267_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace267_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace267_2_0.value, &replace267_2_1.value },
   NULL,
};
static const nir_search_expression replace267 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace267_0.value, &replace267_1.value, &replace267_2.value },
   NULL,
};
   
static const nir_search_variable search268_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search268_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search268_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search268_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search268_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search268_1_0.value, &search268_1_1.value, &search268_1_2.value },
   NULL,
};
static const nir_search_expression search268 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search268_0.value, &search268_1.value },
   NULL,
};
   
static const nir_search_variable replace268_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace268_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace268_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace268_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace268_1_0.value, &replace268_1_1.value },
   NULL,
};

static const nir_search_variable replace268_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace268_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace268_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &replace268_2_0.value, &replace268_2_1.value },
   NULL,
};
static const nir_search_expression replace268 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace268_0.value, &replace268_1.value, &replace268_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ige_xforms[] = {
   { &search128, &replace128.value, 0 },
   { &search267, &replace267.value, 0 },
   { &search268, &replace268.value, 0 },
};
   
static const nir_search_constant search173_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_variable search173_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search173 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fdiv,
   { &search173_0.value, &search173_1.value },
   NULL,
};
   
static const nir_search_variable replace173_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace173 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace173_0.value },
   NULL,
};
   
static const nir_search_variable search174_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search174_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search174 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &search174_0.value, &search174_1.value },
   NULL,
};
   
static const nir_search_variable replace174_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace174_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace174_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &replace174_1_0.value },
   NULL,
};
static const nir_search_expression replace174 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace174_0.value, &replace174_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fdiv_xforms[] = {
   { &search173, &replace173.value, 0 },
   { &search174, &replace174.value, 14 },
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
   
static const nir_search_variable search218_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search218_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search218_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search218_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search218_1_0.value, &search218_1_1.value },
   NULL,
};
static const nir_search_expression search218 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search218_0.value, &search218_1.value },
   NULL,
};
   
static const nir_search_variable replace218_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace218_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace218 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace218_0.value, &replace218_1.value },
   NULL,
};
   
static const nir_search_variable search226_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search226_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search226_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search226_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search226_1_0.value, &search226_1_1.value },
   NULL,
};
static const nir_search_expression search226 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fadd,
   { &search226_0.value, &search226_1.value },
   NULL,
};
   
static const nir_search_variable replace226_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace226_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace226_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace226_0_0.value, &replace226_0_1.value },
   NULL,
};

static const nir_search_variable replace226_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace226 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace226_0.value, &replace226_1.value },
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
   { &search218, &replace218.value, 0 },
   { &search226, &replace226.value, 0 },
};
   
static const nir_search_variable search59_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search59_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search59_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search59_0_0_0.value, &search59_0_0_1.value },
   NULL,
};

static const nir_search_variable search59_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &search59_0_0.value, &search59_0_1.value },
   NULL,
};

static const nir_search_variable search59_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
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

static const nir_search_variable replace59_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace59_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace59_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace59_0_1_0.value, &replace59_0_1_1.value },
   NULL,
};
static const nir_search_expression replace59_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace59_0_0.value, &replace59_0_1.value },
   NULL,
};

static const nir_search_variable replace59_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace59_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace59_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace59_1_0.value, &replace59_1_1.value },
   NULL,
};
static const nir_search_expression replace59 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
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

static const nir_search_variable search60_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search60_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &search60_0_0.value, &search60_0_1.value },
   NULL,
};

static const nir_search_variable search60_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
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

static const nir_search_variable replace60_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace60_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace60_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace60_1_0.value, &replace60_1_1.value },
   NULL,
};
static const nir_search_expression replace60 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace60_0.value, &replace60_1.value },
   NULL,
};
   
static const nir_search_constant search146_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search146_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search146 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search146_0.value, &search146_1.value },
   NULL,
};
   
static const nir_search_constant replace146 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search147_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search147_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search147 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search147_0.value, &search147_1.value },
   NULL,
};
   
static const nir_search_variable replace147 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishl_xforms[] = {
   { &search59, &replace59.value, 0 },
   { &search60, &replace60.value, 0 },
   { &search146, &replace146.value, 0 },
   { &search147, &replace147.value, 0 },
};
   
static const nir_search_variable search196_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search196_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search196_0_0.value },
   NULL,
};
static const nir_search_expression search196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &search196_0.value },
   NULL,
};
   
static const nir_search_variable replace196_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace196 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2u32,
   { &replace196_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_f2u32_xforms[] = {
   { &search196, &replace196.value, 0 },
};
   
static const nir_search_variable search155_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search155_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search155_0_0.value },
   NULL,
};
static const nir_search_expression search155 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search155_0.value },
   NULL,
};
   
static const nir_search_variable replace155 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search168_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search168_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search168_0_0.value },
   NULL,
};
static const nir_search_expression search168 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search168_0.value },
   NULL,
};
   
static const nir_search_constant replace168_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3fe0000000000000 /* 0.5 */ },
};

static const nir_search_variable replace168_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace168_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace168_1_0.value },
   NULL,
};
static const nir_search_expression replace168 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace168_0.value, &replace168_1.value },
   NULL,
};
   
static const nir_search_variable search169_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search169_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search169_0_0.value },
   NULL,
};
static const nir_search_expression search169 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search169_0.value },
   NULL,
};
   
static const nir_search_variable replace169_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace169_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace169_0_0.value },
   NULL,
};
static const nir_search_expression replace169 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace169_0.value },
   NULL,
};
   
static const nir_search_variable search170_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search170_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search170_0_0.value },
   NULL,
};
static const nir_search_expression search170 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search170_0.value },
   NULL,
};
   
static const nir_search_constant replace170_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbfe0000000000000L /* -0.5 */ },
};

static const nir_search_variable replace170_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace170_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace170_1_0.value },
   NULL,
};
static const nir_search_expression replace170 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace170_0.value, &replace170_1.value },
   NULL,
};
   
static const nir_search_variable search171_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search171_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search171_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &search171_0_0.value, &search171_0_1.value },
   NULL,
};
static const nir_search_expression search171 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_flog2,
   { &search171_0.value },
   NULL,
};
   
static const nir_search_variable replace171_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace171_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace171_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &replace171_1_0.value },
   NULL,
};
static const nir_search_expression replace171 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace171_0.value, &replace171_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flog2_xforms[] = {
   { &search155, &replace155.value, 0 },
   { &search168, &replace168.value, 0 },
   { &search169, &replace169.value, 0 },
   { &search170, &replace170.value, 0 },
   { &search171, &replace171.value, 0 },
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
static const nir_search_expression search61_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search61_0_0.value, &search61_0_1.value },
   NULL,
};
static const nir_search_expression search61 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search61_0.value },
   NULL,
};
   
static const nir_search_variable replace61_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace61_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace61 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
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
static const nir_search_expression search62_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search62_0_0.value, &search62_0_1.value },
   NULL,
};
static const nir_search_expression search62 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search62_0.value },
   NULL,
};
   
static const nir_search_variable replace62_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace62_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace62 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace62_0.value, &replace62_1.value },
   NULL,
};
   
static const nir_search_variable search63_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search63_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search63_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search63_0_0.value, &search63_0_1.value },
   NULL,
};
static const nir_search_expression search63 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search63_0.value },
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
   nir_op_fne,
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
static const nir_search_expression search64_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search64_0_0.value, &search64_0_1.value },
   NULL,
};
static const nir_search_expression search64 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_inot,
   { &search64_0.value },
   NULL,
};
   
static const nir_search_variable replace64_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace64_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace64 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace64_0.value, &replace64_1.value },
   NULL,
};
   
static const nir_search_variable search65_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search65_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search65_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &search65_0_0.value, &search65_0_1.value },
   NULL,
};
static const nir_search_expression search65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search65_0.value },
   NULL,
};
   
static const nir_search_variable replace65_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace65_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace65 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
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
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search66_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ige,
   { &search66_0_0.value, &search66_0_1.value },
   NULL,
};
static const nir_search_expression search66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search66_0.value },
   NULL,
};
   
static const nir_search_variable replace66_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace66_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace66 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace66_0.value, &replace66_1.value },
   NULL,
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
   nir_op_ieq,
   { &search67_0_0.value, &search67_0_1.value },
   NULL,
};
static const nir_search_expression search67 = {
   { nir_search_value_expression, 0 },
   false,
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
   nir_op_ine,
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
   nir_op_ine,
   { &search68_0_0.value, &search68_0_1.value },
   NULL,
};
static const nir_search_expression search68 = {
   { nir_search_value_expression, 0 },
   false,
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
   nir_op_ieq,
   { &replace68_0.value, &replace68_1.value },
   NULL,
};
   
static const nir_search_variable search143_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search143_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search143_0_0.value },
   NULL,
};
static const nir_search_expression search143 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search143_0.value },
   NULL,
};
   
static const nir_search_variable replace143 = {
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
   { &search143, &replace143.value, 0 },
};
   
static const nir_search_variable search117_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search117_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_sne,
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

static const nir_search_variable replace117_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace117_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace117_0_0.value, &replace117_0_1.value },
   NULL,
};
static const nir_search_expression replace117 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace117_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_sne_xforms[] = {
   { &search117, &replace117.value, 11 },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search228 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_fmod,
   { &search228_0.value, &search228_1.value },
   NULL,
};
   
static const nir_search_variable replace228_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace228_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace228_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace228_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace228_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace228_1_1_0_0.value, &replace228_1_1_0_1.value },
   NULL,
};
static const nir_search_expression replace228_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace228_1_1_0.value },
   NULL,
};
static const nir_search_expression replace228_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace228_1_0.value, &replace228_1_1.value },
   NULL,
};
static const nir_search_expression replace228 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace228_0.value, &replace228_1.value },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search229 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_fmod,
   { &search229_0.value, &search229_1.value },
   NULL,
};
   
static const nir_search_variable replace229_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace229_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace229_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace229_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace229_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace229_1_1_0_0.value, &replace229_1_1_0_1.value },
   NULL,
};
static const nir_search_expression replace229_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ffloor,
   { &replace229_1_1_0.value },
   NULL,
};
static const nir_search_expression replace229_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace229_1_0.value, &replace229_1_1.value },
   NULL,
};
static const nir_search_expression replace229 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace229_0.value, &replace229_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmod_xforms[] = {
   { &search228, &replace228.value, 21 },
   { &search229, &replace229.value, 22 },
};
   
static const nir_search_variable search203_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search203_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_x,
   { &search203_0_0.value },
   NULL,
};

static const nir_search_variable search203_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search203_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_y,
   { &search203_1_0.value },
   NULL,
};
static const nir_search_expression search203 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &search203_0.value, &search203_1.value },
   NULL,
};
   
static const nir_search_variable replace203 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_pack_64_2x32_split_xforms[] = {
   { &search203, &replace203.value, 0 },
};
   
static const nir_search_variable search193_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search193_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search193_2 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search193 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fcsel,
   { &search193_0.value, &search193_1.value, &search193_2.value },
   NULL,
};
   
static const nir_search_variable replace193 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_fcsel_xforms[] = {
   { &search193, &replace193.value, 0 },
};
   
static const nir_search_variable search211_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search211_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search211_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search211_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search211_1_0.value, &search211_1_1.value },
   NULL,
};
static const nir_search_expression search211 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search211_0.value, &search211_1.value },
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
   nir_op_iadd,
   { &replace211_0.value, &replace211_1.value },
   NULL,
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
   nir_op_isub,
   { &search215_0.value, &search215_1.value },
   NULL,
};
   
static const nir_search_variable replace215_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace215_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace215_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &replace215_1_0.value },
   NULL,
};
static const nir_search_expression replace215 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace215_0.value, &replace215_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_isub_xforms[] = {
   { &search211, &replace211.value, 0 },
   { &search215, &replace215.value, 19 },
};
   
static const nir_search_variable search79_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   false,
   nir_op_fmax,
   { &search79_0.value, &search79_1.value },
   NULL,
};
   
static const nir_search_variable replace79 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search90_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search90_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search90_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search90_1_0_0.value },
   NULL,
};
static const nir_search_expression search90_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search90_1_0.value },
   NULL,
};
static const nir_search_expression search90 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
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
   
static const nir_search_variable search92_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search92_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search92_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
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
   
static const nir_search_variable replace92_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace92 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace92_0.value },
   NULL,
};
   
static const nir_search_variable search94_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search94_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search94_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search94_1_0.value },
   NULL,
};
static const nir_search_expression search94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search94_0.value, &search94_1.value },
   NULL,
};
   
static const nir_search_variable replace94_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace94 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace94_0.value },
   NULL,
};
   
static const nir_search_variable search97_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search97_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search97_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search97_0_0.value, &search97_0_1.value },
   NULL,
};

static const nir_search_constant search97_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search97 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmax,
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
static const nir_search_expression replace97 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace97_0.value },
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
   nir_op_fsat,
   { &search103_0_0.value },
   NULL,
};

static const nir_search_variable search103_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_zero_to_one),
};
static const nir_search_expression search103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search103_0.value, &search103_1.value },
   NULL,
};
   
static const nir_search_variable replace103_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace103_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace103_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace103_0_0.value, &replace103_0_1.value },
   NULL,
};
static const nir_search_expression replace103 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace103_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmax_xforms[] = {
   { &search79, &replace79.value, 0 },
   { &search90, &replace90.value, 0 },
   { &search92, &replace92.value, 0 },
   { &search94, &replace94.value, 0 },
   { &search97, &replace97.value, 9 },
   { &search103, &replace103.value, 0 },
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
   
static const nir_search_variable search233_0 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search233_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search233_2 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search233_3 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_insert,
   { &search233_0.value, &search233_1.value, &search233_2.value, &search233_3.value },
   NULL,
};
   
static const nir_search_constant replace233_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace233_0_1 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace233_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace233_0_0.value, &replace233_0_1.value },
   NULL,
};

static const nir_search_variable replace233_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace233_2_0_0 = {
   { nir_search_value_variable, 0 },
   3, /* bits */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace233_2_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* offset */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace233_2_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfm,
   { &replace233_2_0_0.value, &replace233_2_0_1.value },
   NULL,
};

static const nir_search_variable replace233_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* insert */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace233_2_2 = {
   { nir_search_value_variable, 0 },
   0, /* base */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace233_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bfi,
   { &replace233_2_0.value, &replace233_2_1.value, &replace233_2_2.value },
   NULL,
};
static const nir_search_expression replace233 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace233_0.value, &replace233_1.value, &replace233_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_bitfield_insert_xforms[] = {
   { &search233, &replace233.value, 25 },
};
   
static const nir_search_variable search119_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search119_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search119_0_0.value },
   NULL,
};

static const nir_search_variable search119_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search119 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search119_0.value, &search119_1.value },
   NULL,
};
   
static const nir_search_variable replace119_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace119_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace119 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace119_0.value, &replace119_1.value },
   NULL,
};
   
static const nir_search_variable search261_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search261_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search261_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search261_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search261_0_0.value, &search261_0_1.value, &search261_0_2.value },
   NULL,
};

static const nir_search_variable search261_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search261 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search261_0.value, &search261_1.value },
   NULL,
};
   
static const nir_search_variable replace261_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace261_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace261_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace261_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace261_1_0.value, &replace261_1_1.value },
   NULL,
};

static const nir_search_variable replace261_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace261_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace261_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace261_2_0.value, &replace261_2_1.value },
   NULL,
};
static const nir_search_expression replace261 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace261_0.value, &replace261_1.value, &replace261_2.value },
   NULL,
};
   
static const nir_search_variable search262_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search262_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search262_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search262_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search262_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search262_1_0.value, &search262_1_1.value, &search262_1_2.value },
   NULL,
};
static const nir_search_expression search262 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &search262_0.value, &search262_1.value },
   NULL,
};
   
static const nir_search_variable replace262_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace262_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace262_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace262_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace262_1_0.value, &replace262_1_1.value },
   NULL,
};

static const nir_search_variable replace262_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace262_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace262_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace262_2_0.value, &replace262_2_1.value },
   NULL,
};
static const nir_search_expression replace262 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace262_0.value, &replace262_1.value, &replace262_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_feq_xforms[] = {
   { &search119, &replace119.value, 0 },
   { &search261, &replace261.value, 0 },
   { &search262, &replace262.value, 0 },
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
   
static const nir_search_variable search106_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search106_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search106_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search106_0_0.value, &search106_0_1.value },
   NULL,
};

static const nir_search_variable search106_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search106_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search106_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search106_1_0.value, &search106_1_1.value },
   NULL,
};
static const nir_search_expression search106 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search106_0.value, &search106_1.value },
   NULL,
};
   
static const nir_search_variable replace106_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace106_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace106_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace106_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace106_1_0.value, &replace106_1_1.value },
   NULL,
};
static const nir_search_expression replace106 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace106_0.value, &replace106_1.value },
   NULL,
};
   
static const nir_search_variable search107_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search107_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search107_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search107_0_0.value, &search107_0_1.value },
   NULL,
};

static const nir_search_variable search107_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search107_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search107_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search107_1_0.value, &search107_1_1.value },
   NULL,
};
static const nir_search_expression search107 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search107_0.value, &search107_1.value },
   NULL,
};
   
static const nir_search_variable replace107_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace107_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace107_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace107_0_0.value, &replace107_0_1.value },
   NULL,
};

static const nir_search_variable replace107_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace107 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace107_0.value, &replace107_1.value },
   NULL,
};
   
static const nir_search_variable search108_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search108_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search108_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search108_0_0.value, &search108_0_1.value },
   NULL,
};

static const nir_search_variable search108_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search108_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search108_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search108_1_0.value, &search108_1_1.value },
   NULL,
};
static const nir_search_expression search108 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search108_0.value, &search108_1.value },
   NULL,
};
   
static const nir_search_variable replace108_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace108_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace108_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace108_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace108_1_0.value, &replace108_1_1.value },
   NULL,
};
static const nir_search_expression replace108 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace108_0.value, &replace108_1.value },
   NULL,
};
   
static const nir_search_variable search109_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search109_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search109_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search109_0_0.value, &search109_0_1.value },
   NULL,
};

static const nir_search_variable search109_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search109_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search109_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search109_1_0.value, &search109_1_1.value },
   NULL,
};
static const nir_search_expression search109 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_ior,
   { &search109_0.value, &search109_1.value },
   NULL,
};
   
static const nir_search_variable replace109_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace109_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace109_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace109_0_0.value, &replace109_0_1.value },
   NULL,
};

static const nir_search_variable replace109_1 = {
   { nir_search_value_variable, 0 },
   1, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace109 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace109_0.value, &replace109_1.value },
   NULL,
};
   
static const nir_search_variable search137_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search137_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search137 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search137_0.value, &search137_1.value },
   NULL,
};
   
static const nir_search_variable replace137 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search138_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search138_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search138 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search138_0.value, &search138_1.value },
   NULL,
};
   
static const nir_search_variable replace138 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search139_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search139_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search139 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search139_0.value, &search139_1.value },
   NULL,
};
   
static const nir_search_constant replace139 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
   
static const nir_search_variable search145_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search145_0_0.value },
   NULL,
};

static const nir_search_variable search145_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search145_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search145_1_0.value },
   NULL,
};
static const nir_search_expression search145 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace145_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace145_0_0.value, &replace145_0_1.value },
   NULL,
};
static const nir_search_expression replace145 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace145_0.value },
   NULL,
};
   
static const nir_search_variable search250_0_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search250_0_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search250_0_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_0_0_0_1_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0_0_0_0_1_0_0.value, &search250_0_0_0_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_0_0_0_1_0.value, &search250_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search250_0_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_1_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0_1_0_0_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_1_0_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search250_0_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_1_0_0_1_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0_1_0_0_1_0_0.value, &search250_0_0_0_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_1_0_0_1_0.value, &search250_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_1_0_0_0.value, &search250_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0_1_0_0.value, &search250_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_0_0_0_1_0.value, &search250_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0_0_0_0.value, &search250_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
static const nir_search_expression search250_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_0_0_0.value, &search250_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search250_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_0_0.value, &search250_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search250_0_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_0_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search250_0_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_0_0_0_1_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0_0_0_0_1_0_0.value, &search250_0_0_0_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_0_0_0_1_0.value, &search250_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0_0_0_0.value, &search250_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_0_0.value, &search250_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search250_0_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_1_0_0_0_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0_1_0_0_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_1_0_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_0_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search250_0_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_1_0_0_1_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0_1_0_0_1_0_0.value, &search250_0_0_0_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_1_0_0_1_0.value, &search250_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_1_0_0_0.value, &search250_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0_1_0_0.value, &search250_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0_0_1_0.value, &search250_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_1_0_0_0.value, &search250_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
static const nir_search_expression search250_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0_1_0_0.value, &search250_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search250_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_0_0_0_1_0.value, &search250_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0_0_0_0.value, &search250_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x55555555 /* 1431655765 */ },
};
static const nir_search_expression search250_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_0_0_0.value, &search250_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search250_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_0_0.value, &search250_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_0_0_0_0_0_0_1_0.value, &search250_1_0_0_0_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_0_0_0_1_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_0_0_0_1_0_0_1_0.value, &search250_1_0_0_0_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_0_0_0_1_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0_0_0_0_1_0_0.value, &search250_1_0_0_0_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_0_0_0_1_0.value, &search250_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_1_0_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_1_0_0_0_0_0_1_0.value, &search250_1_0_0_0_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_1_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0_1_0_0_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_1_0_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0_0_1_0_0_1_0_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_0_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_1_0_0_1_0_0_1_0.value, &search250_1_0_0_0_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_1_0_0_1_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0_1_0_0_1_0_0.value, &search250_1_0_0_0_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_1_0_0_1_0.value, &search250_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_1_0_0_0.value, &search250_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0_1_0_0.value, &search250_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_0_0_0_1_0.value, &search250_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0_0_0_0.value, &search250_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x33333333 /* 858993459 */ },
};
static const nir_search_expression search250_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_0_0_0.value, &search250_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search250_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_0_0.value, &search250_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_0_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_0_0_0_0_0_0_1_0.value, &search250_1_0_0_1_0_0_0_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_0_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_0_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_0_0_0_1_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_0_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_0_0_0_1_0_0_1_0.value, &search250_1_0_0_1_0_0_0_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_0_0_0_1_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0_0_0_0_1_0_0.value, &search250_1_0_0_1_0_0_0_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_0_0_0_1_0.value, &search250_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f /* 252645135 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0_0_0_0.value, &search250_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_0_0.value, &search250_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_1_0_0_0_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_1_0_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_1_0_0_0_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_0_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_0_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_1_0_0_0_0_0_1_0.value, &search250_1_0_0_1_0_0_1_0_0_0_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_1_0_0_0_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff /* 16711935 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0_1_0_0_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_1_0_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_1_0_0_1_0_0_0_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &search250_1_0_0_1_0_0_1_0_0_1_0_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search250_1_0_0_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_variable, 32 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_1_0_0_1_0_0_1_0.value, &search250_1_0_0_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_1_0_0_1_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xff00ff00 /* 4278255360 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0_1_0_0_1_0_0.value, &search250_1_0_0_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_1_0_0_1_0.value, &search250_1_0_0_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_1_0_0_0.value, &search250_1_0_0_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xf0f0f0f0 /* 4042322160 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0_1_0_0.value, &search250_1_0_0_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x4 /* 4 */ },
};
static const nir_search_expression search250_1_0_0_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0_0_1_0.value, &search250_1_0_0_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_1_0_0_0.value, &search250_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xcccccccc /* 3435973836 */ },
};
static const nir_search_expression search250_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0_1_0_0.value, &search250_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression search250_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0_0_1_0.value, &search250_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression search250_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_1_0_0_0.value, &search250_1_0_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xaaaaaaaa /* 2863311530 */ },
};
static const nir_search_expression search250_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &search250_1_0_0.value, &search250_1_0_1.value },
   NULL,
};

static const nir_search_constant search250_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression search250_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search250_1_0.value, &search250_1_1.value },
   NULL,
};
static const nir_search_expression search250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &search250_0.value, &search250_1.value },
   NULL,
};
   
static const nir_search_variable replace250_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace250 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bitfield_reverse,
   { &replace250_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ior_xforms[] = {
   { &search106, &replace106.value, 0 },
   { &search107, &replace107.value, 0 },
   { &search108, &replace108.value, 0 },
   { &search109, &replace109.value, 0 },
   { &search137, &replace137.value, 0 },
   { &search138, &replace138.value, 0 },
   { &search139, &replace139.value, 0 },
   { &search145, &replace145.value, 0 },
   { &search250, &replace250.value, 0 },
};
   
static const nir_search_variable search212_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search212_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search212 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search212_0.value, &search212_1.value },
   NULL,
};
   
static const nir_search_variable replace212 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search213_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search213_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x1 /* -1 */ },
};
static const nir_search_expression search213 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ussub_4x8,
   { &search213_0.value, &search213_1.value },
   NULL,
};
   
static const nir_search_constant replace213 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const struct transform nir_opt_algebraic_ussub_4x8_xforms[] = {
   { &search212, &replace212.value, 0 },
   { &search213, &replace213.value, 0 },
};
   
static const nir_search_variable search247_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_4x8,
   { &search247_0.value },
   NULL,
};
   
static const nir_search_constant replace247_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace247_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace247_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace247_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace247_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace247_1_1_0_0_0_0.value, &replace247_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace247_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace247_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace247_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace247_1_1_0_0_1_0.value, &replace247_1_1_0_0_1_1.value },
   NULL,
};

static const nir_search_variable replace247_1_1_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace247_1_1_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace247_1_1_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace247_1_1_0_0_2_0.value, &replace247_1_1_0_0_2_1.value },
   NULL,
};

static const nir_search_variable replace247_1_1_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace247_1_1_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace247_1_1_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &replace247_1_1_0_0_3_0.value, &replace247_1_1_0_0_3_1.value },
   NULL,
};
static const nir_search_expression replace247_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace247_1_1_0_0_0.value, &replace247_1_1_0_0_1.value, &replace247_1_1_0_0_2.value, &replace247_1_1_0_0_3.value },
   NULL,
};
static const nir_search_expression replace247_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f32,
   { &replace247_1_1_0_0.value },
   NULL,
};

static const nir_search_constant replace247_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x405fc00000000000 /* 127.0 */ },
};
static const nir_search_expression replace247_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace247_1_1_0.value, &replace247_1_1_1.value },
   NULL,
};
static const nir_search_expression replace247_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace247_1_0.value, &replace247_1_1.value },
   NULL,
};
static const nir_search_expression replace247 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace247_0.value, &replace247_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_snorm_4x8_xforms[] = {
   { &search247, &replace247.value, 36 },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frem,
   { &search230_0.value, &search230_1.value },
   NULL,
};
   
static const nir_search_variable replace230_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace230_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace230_1_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace230_1_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace230_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace230_1_1_0_0.value, &replace230_1_1_0_1.value },
   NULL,
};
static const nir_search_expression replace230_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &replace230_1_1_0.value },
   NULL,
};
static const nir_search_expression replace230_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace230_1_0.value, &replace230_1_1.value },
   NULL,
};
static const nir_search_expression replace230 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace230_0.value, &replace230_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frem_xforms[] = {
   { &search230, &replace230.value, 21 },
};
   
static const nir_search_variable search202_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
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
   nir_op_pack_64_2x32_split,
   { &search202_0_0.value, &search202_0_1.value },
   NULL,
};
static const nir_search_expression search202 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_64_2x32_split_y,
   { &search202_0.value },
   NULL,
};
   
static const nir_search_variable replace202 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_64_2x32_split_y_xforms[] = {
   { &search202, &replace202.value, 0 },
};
   
static const nir_search_variable search244_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search244 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_2x16,
   { &search244_0.value },
   NULL,
};
   
static const nir_search_variable replace244_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace244_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace244_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace244_0_0_0_0.value, &replace244_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace244_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace244_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace244_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace244_0_0_1_0.value, &replace244_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace244_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace244_0_0_0.value, &replace244_0_0_1.value },
   NULL,
};
static const nir_search_expression replace244_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f32,
   { &replace244_0_0.value },
   NULL,
};

static const nir_search_constant replace244_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40efffe000000000 /* 65535.0 */ },
};
static const nir_search_expression replace244 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace244_0.value, &replace244_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_unorm_2x16_xforms[] = {
   { &search244, &replace244.value, 33 },
};
   
static const nir_search_variable search246_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_snorm_2x16,
   { &search246_0.value },
   NULL,
};
   
static const nir_search_constant replace246_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};

static const nir_search_constant replace246_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};

static const nir_search_variable replace246_1_1_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace246_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace246_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace246_1_1_0_0_0_0.value, &replace246_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace246_1_1_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace246_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace246_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &replace246_1_1_0_0_1_0.value, &replace246_1_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace246_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec2,
   { &replace246_1_1_0_0_0.value, &replace246_1_1_0_0_1.value },
   NULL,
};
static const nir_search_expression replace246_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_i2f32,
   { &replace246_1_1_0_0.value },
   NULL,
};

static const nir_search_constant replace246_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x40dfffc000000000 /* 32767.0 */ },
};
static const nir_search_expression replace246_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace246_1_1_0.value, &replace246_1_1_1.value },
   NULL,
};
static const nir_search_expression replace246_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace246_1_0.value, &replace246_1_1.value },
   NULL,
};
static const nir_search_expression replace246 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace246_0.value, &replace246_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_snorm_2x16_xforms[] = {
   { &search246, &replace246.value, 35 },
};
   
static const nir_search_variable search81_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
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
   false,
   nir_op_imax,
   { &search81_0.value, &search81_1.value },
   NULL,
};
   
static const nir_search_variable replace81 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search91_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search91_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search91_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search91_1_0_0.value },
   NULL,
};
static const nir_search_expression search91_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search91_1_0.value },
   NULL,
};
static const nir_search_expression search91 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search91_0.value, &search91_1.value },
   NULL,
};
   
static const nir_search_variable replace91 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search93_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search93_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search93_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search93_1_0.value },
   NULL,
};
static const nir_search_expression search93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search93_0.value, &search93_1.value },
   NULL,
};
   
static const nir_search_variable replace93_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace93 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace93_0.value },
   NULL,
};
   
static const nir_search_variable search95_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search95_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search95_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ineg,
   { &search95_1_0.value },
   NULL,
};
static const nir_search_expression search95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &search95_0.value, &search95_1.value },
   NULL,
};
   
static const nir_search_variable replace95_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace95 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace95_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_imax_xforms[] = {
   { &search81, &replace81.value, 0 },
   { &search91, &replace91.value, 0 },
   { &search93, &replace93.value, 0 },
   { &search95, &replace95.value, 0 },
};
   
static const nir_search_variable search98_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search98_0.value },
   NULL,
};
   
static const nir_search_variable replace98_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace98_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace98_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace98_0_0.value, &replace98_0_1.value },
   NULL,
};

static const nir_search_constant replace98_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace98 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace98_0.value, &replace98_1.value },
   NULL,
};
   
static const nir_search_variable search99_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search99_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search99_0_0.value },
   NULL,
};
static const nir_search_expression search99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search99_0.value },
   NULL,
};
   
static const nir_search_variable replace99_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace99 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace99_0.value },
   NULL,
};
   
static const nir_search_variable search122_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search122_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search122_0_0_0.value },
   NULL,
};

static const nir_search_variable search122_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search122_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search122_0_1_0.value },
   NULL,
};
static const nir_search_expression search122_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search122_0_0.value, &search122_0_1.value },
   NULL,
};
static const nir_search_expression search122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search122_0.value },
   NULL,
};
   
static const nir_search_variable replace122_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace122_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace122_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ior,
   { &replace122_0_0.value, &replace122_0_1.value },
   NULL,
};
static const nir_search_expression replace122 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace122_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsat_xforms[] = {
   { &search98, &replace98.value, 10 },
   { &search99, &replace99.value, 0 },
   { &search122, &replace122.value, 0 },
};
   
static const nir_search_variable search239_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search239_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &search239_0.value, &search239_1.value },
   NULL,
};
   
static const nir_search_variable replace239_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace239_0_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace239_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace239_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace239_0_1_0.value, &replace239_0_1_1.value },
   NULL,
};
static const nir_search_expression replace239_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &replace239_0_0.value, &replace239_0_1.value },
   NULL,
};

static const nir_search_constant replace239_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xffff /* 65535 */ },
};
static const nir_search_expression replace239 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iand,
   { &replace239_0.value, &replace239_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_u16_xforms[] = {
   { &search239, &replace239.value, 28 },
};
   
static const nir_search_constant search69_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search69_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search69_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search69_1_0.value },
   NULL,
};
static const nir_search_expression search69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search69_0.value, &search69_1.value },
   NULL,
};
   
static const nir_search_variable replace69_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace69 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace69_0.value },
   NULL,
};
   
static const nir_search_variable search70_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search70_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search70_0_0_0.value },
   NULL,
};
static const nir_search_expression search70_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search70_0_0.value },
   NULL,
};

static const nir_search_constant search70_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search70_0.value, &search70_1.value },
   NULL,
};
   
static const nir_search_variable replace70_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace70 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace70_0.value },
   NULL,
};
   
static const nir_search_variable search72_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search72_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search72_0_0_0.value },
   NULL,
};
static const nir_search_expression search72_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search72_0_0.value },
   NULL,
};

static const nir_search_constant search72_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search72_0.value, &search72_1.value },
   NULL,
};
   
static const nir_search_variable replace72_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace72_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace72 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace72_0.value, &replace72_1.value },
   NULL,
};
   
static const nir_search_variable search259_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search259_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search259_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search259_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search259_0_0.value, &search259_0_1.value, &search259_0_2.value },
   NULL,
};

static const nir_search_variable search259_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search259 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search259_0.value, &search259_1.value },
   NULL,
};
   
static const nir_search_variable replace259_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace259_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace259_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace259_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace259_1_0.value, &replace259_1_1.value },
   NULL,
};

static const nir_search_variable replace259_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace259_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace259_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace259_2_0.value, &replace259_2_1.value },
   NULL,
};
static const nir_search_expression replace259 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace259_0.value, &replace259_1.value, &replace259_2.value },
   NULL,
};
   
static const nir_search_variable search260_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search260_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search260_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search260_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search260_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search260_1_0.value, &search260_1_1.value, &search260_1_2.value },
   NULL,
};
static const nir_search_expression search260 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &search260_0.value, &search260_1.value },
   NULL,
};
   
static const nir_search_variable replace260_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace260_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace260_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace260_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace260_1_0.value, &replace260_1_1.value },
   NULL,
};

static const nir_search_variable replace260_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace260_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace260_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace260_2_0.value, &replace260_2_1.value },
   NULL,
};
static const nir_search_expression replace260 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace260_0.value, &replace260_1.value, &replace260_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fge_xforms[] = {
   { &search69, &replace69.value, 0 },
   { &search70, &replace70.value, 0 },
   { &search72, &replace72.value, 0 },
   { &search259, &replace259.value, 0 },
   { &search260, &replace260.value, 0 },
};
   
static const nir_search_variable search166_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search166_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &search166_0_0.value },
   NULL,
};
static const nir_search_expression search166 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search166_0.value },
   NULL,
};
   
static const nir_search_variable replace166_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace166_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace166_0_0.value },
   NULL,
};
static const nir_search_expression replace166 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fexp2,
   { &replace166_0.value },
   NULL,
};
   
static const nir_search_variable search175_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search175_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frcp,
   { &search175_0_0.value },
   NULL,
};
static const nir_search_expression search175 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search175_0.value },
   NULL,
};
   
static const nir_search_variable replace175 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search176_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search176_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &search176_0_0.value },
   NULL,
};
static const nir_search_expression search176 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search176_0.value },
   NULL,
};
   
static const nir_search_variable replace176_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace176 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &replace176_0.value },
   NULL,
};
   
static const nir_search_variable search178_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search178_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_frsq,
   { &search178_0_0.value },
   NULL,
};
static const nir_search_expression search178 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_frcp,
   { &search178_0.value },
   NULL,
};
   
static const nir_search_variable replace178_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace178 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsqrt,
   { &replace178_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_frcp_xforms[] = {
   { &search166, &replace166.value, 0 },
   { &search175, &replace175.value, 0 },
   { &search176, &replace176.value, 0 },
   { &search178, &replace178.value, 16 },
};
   
static const nir_search_variable search140_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search140_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search140 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fxor,
   { &search140_0.value, &search140_1.value },
   NULL,
};
   
static const nir_search_constant replace140 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const struct transform nir_opt_algebraic_fxor_xforms[] = {
   { &search140, &replace140.value, 0 },
};
   
static const nir_search_constant search150_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search150_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search150 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search150_0.value, &search150_1.value },
   NULL,
};
   
static const nir_search_constant replace150 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search151_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search151_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search151 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search151_0.value, &search151_1.value },
   NULL,
};
   
static const nir_search_variable replace151 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search204_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search204_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression search204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search204_0.value, &search204_1.value },
   NULL,
};
   
static const nir_search_variable replace204_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace204_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace204 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace204_0.value, &replace204_1.value },
   NULL,
};
   
static const nir_search_variable search208_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search208_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression search208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ushr,
   { &search208_0.value, &search208_1.value },
   NULL,
};
   
static const nir_search_variable replace208_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace208_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace208 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u16,
   { &replace208_0.value, &replace208_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ushr_xforms[] = {
   { &search150, &replace150.value, 0 },
   { &search151, &replace151.value, 0 },
   { &search204, &replace204.value, 17 },
   { &search208, &replace208.value, 18 },
};
   
static const nir_search_variable search154_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search154_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search154_0_0.value },
   NULL,
};
static const nir_search_expression search154 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search154_0.value },
   NULL,
};
   
static const nir_search_variable replace154 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search157_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search157_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search157_0_0_0.value },
   NULL,
};

static const nir_search_variable search157_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search157_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search157_0_0.value, &search157_0_1.value },
   NULL,
};
static const nir_search_expression search157 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search157_0.value },
   NULL,
};
   
static const nir_search_variable replace157_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace157_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace157 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace157_0.value, &replace157_1.value },
   NULL,
};
   
static const nir_search_variable search158_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search158_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search158_0_0_0_0.value },
   NULL,
};

static const nir_search_variable search158_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search158_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search158_0_0_0.value, &search158_0_0_1.value },
   NULL,
};

static const nir_search_variable search158_0_1_0_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search158_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flog2,
   { &search158_0_1_0_0.value },
   NULL,
};

static const nir_search_variable search158_0_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search158_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &search158_0_1_0.value, &search158_0_1_1.value },
   NULL,
};
static const nir_search_expression search158_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search158_0_0.value, &search158_0_1.value },
   NULL,
};
static const nir_search_expression search158 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fexp2,
   { &search158_0.value },
   NULL,
};
   
static const nir_search_variable replace158_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace158_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace158_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace158_0_0.value, &replace158_0_1.value },
   NULL,
};

static const nir_search_variable replace158_1_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace158_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace158_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fpow,
   { &replace158_1_0.value, &replace158_1_1.value },
   NULL,
};
static const nir_search_expression replace158 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fmul,
   { &replace158_0.value, &replace158_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fexp2_xforms[] = {
   { &search154, &replace154.value, 0 },
   { &search157, &replace157.value, 13 },
   { &search158, &replace158.value, 13 },
};
   
static const nir_search_constant search148_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search148_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search148 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search148_0.value, &search148_1.value },
   NULL,
};
   
static const nir_search_constant replace148 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
   
static const nir_search_variable search149_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search149_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search149 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &search149_0.value, &search149_1.value },
   NULL,
};
   
static const nir_search_variable replace149 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const struct transform nir_opt_algebraic_ishr_xforms[] = {
   { &search148, &replace148.value, 0 },
   { &search149, &replace149.value, 0 },
};
   
static const nir_search_variable search235_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search235_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search235_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubitfield_extract,
   { &search235_0.value, &search235_1.value, &search235_2.value },
   NULL,
};
   
static const nir_search_constant replace235_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace235_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace235_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace235_0_0.value, &replace235_0_1.value },
   NULL,
};

static const nir_search_variable replace235_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace235_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace235_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace235_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace235_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ubfe,
   { &replace235_2_0.value, &replace235_2_1.value, &replace235_2_2.value },
   NULL,
};
static const nir_search_expression replace235 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace235_0.value, &replace235_1.value, &replace235_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ubitfield_extract_xforms[] = {
   { &search235, &replace235.value, 26 },
};
   
static const nir_search_variable search114_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search114_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_slt,
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

static const nir_search_variable replace114_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace114_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace114_0_0.value, &replace114_0_1.value },
   NULL,
};
static const nir_search_expression replace114 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &replace114_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_slt_xforms[] = {
   { &search114, &replace114.value, 11 },
};
   
static const nir_search_constant search71_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search71_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search71_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search71_1_0.value },
   NULL,
};
static const nir_search_expression search71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search71_0.value, &search71_1.value },
   NULL,
};
   
static const nir_search_variable replace71_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace71_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace71 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace71_0.value, &replace71_1.value },
   NULL,
};
   
static const nir_search_variable search125_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search125_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search125_0_0_0.value },
   NULL,
};
static const nir_search_expression search125_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search125_0_0.value },
   NULL,
};

static const nir_search_constant search125_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search125 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search125_0.value, &search125_1.value },
   NULL,
};
   
static const nir_search_variable replace125 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_constant search126_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search126_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search126_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search126_0_1_0.value },
   NULL,
};
static const nir_search_expression search126_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search126_0_0.value, &search126_0_1.value },
   NULL,
};

static const nir_search_constant search126_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression search126 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search126_0.value, &search126_1.value },
   NULL,
};
   
static const nir_search_variable replace126 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search257_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search257_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search257_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search257_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search257_0_0.value, &search257_0_1.value, &search257_0_2.value },
   NULL,
};

static const nir_search_variable search257_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search257 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
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

static const nir_search_variable replace257_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace257_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace257_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace257_1_0.value, &replace257_1_1.value },
   NULL,
};

static const nir_search_variable replace257_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace257_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace257_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace257_2_0.value, &replace257_2_1.value },
   NULL,
};
static const nir_search_expression replace257 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace257_0.value, &replace257_1.value, &replace257_2.value },
   NULL,
};
   
static const nir_search_variable search258_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search258_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search258_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search258_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search258_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search258_1_0.value, &search258_1_1.value, &search258_1_2.value },
   NULL,
};
static const nir_search_expression search258 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search258_0.value, &search258_1.value },
   NULL,
};
   
static const nir_search_variable replace258_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace258_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace258_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace258_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace258_1_0.value, &replace258_1_1.value },
   NULL,
};

static const nir_search_variable replace258_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace258_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace258_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace258_2_0.value, &replace258_2_1.value },
   NULL,
};
static const nir_search_expression replace258 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace258_0.value, &replace258_1.value, &replace258_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_flt_xforms[] = {
   { &search71, &replace71.value, 0 },
   { &search125, &replace125.value, 0 },
   { &search126, &replace126.value, 0 },
   { &search257, &replace257.value, 0 },
   { &search258, &replace258.value, 0 },
};
   
static const nir_search_variable search131_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search131_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search131 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search131_0.value, &search131_1.value },
   NULL,
};
   
static const nir_search_constant replace131 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search273_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search273_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search273_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search273_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search273_0_0.value, &search273_0_1.value, &search273_0_2.value },
   NULL,
};

static const nir_search_variable search273_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search273 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search273_0.value, &search273_1.value },
   NULL,
};
   
static const nir_search_variable replace273_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace273_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace273_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace273_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace273_1_0.value, &replace273_1_1.value },
   NULL,
};

static const nir_search_variable replace273_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace273_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace273_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace273_2_0.value, &replace273_2_1.value },
   NULL,
};
static const nir_search_expression replace273 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace273_0.value, &replace273_1.value, &replace273_2.value },
   NULL,
};
   
static const nir_search_variable search274_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search274_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search274_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search274_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search274_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search274_1_0.value, &search274_1_1.value, &search274_1_2.value },
   NULL,
};
static const nir_search_expression search274 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &search274_0.value, &search274_1.value },
   NULL,
};
   
static const nir_search_variable replace274_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace274_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace274_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace274_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace274_1_0.value, &replace274_1_1.value },
   NULL,
};

static const nir_search_variable replace274_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace274_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace274_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace274_2_0.value, &replace274_2_1.value },
   NULL,
};
static const nir_search_expression replace274 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace274_0.value, &replace274_1.value, &replace274_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ult_xforms[] = {
   { &search131, &replace131.value, 0 },
   { &search273, &replace273.value, 0 },
   { &search274, &replace274.value, 0 },
};
   
static const nir_search_variable search195_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search195_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ftrunc,
   { &search195_0_0.value },
   NULL,
};
static const nir_search_expression search195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &search195_0.value },
   NULL,
};
   
static const nir_search_variable replace195_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace195 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_f2i32,
   { &replace195_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_f2i32_xforms[] = {
   { &search195, &replace195.value, 0 },
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
   
static const nir_search_variable search216_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search216_0.value },
   NULL,
};
   
static const nir_search_constant replace216_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable replace216_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace216 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &replace216_0.value, &replace216_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fneg_xforms[] = {
   { &search10, &replace10.value, 0 },
   { &search216, &replace216.value, 20 },
};
   
static const nir_search_variable search210_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search210_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_variable search210_1_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search210_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search210_1_0.value, &search210_1_1.value },
   NULL,
};
static const nir_search_expression search210 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fsub,
   { &search210_0.value, &search210_1.value },
   NULL,
};
   
static const nir_search_variable replace210_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace210_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace210 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace210_0.value, &replace210_1.value },
   NULL,
};
   
static const nir_search_variable search214_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search214_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsub,
   { &search214_0.value, &search214_1.value },
   NULL,
};
   
static const nir_search_variable replace214_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace214_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace214_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace214_1_0.value },
   NULL,
};
static const nir_search_expression replace214 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace214_0.value, &replace214_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fsub_xforms[] = {
   { &search210, &replace210.value, 0 },
   { &search214, &replace214.value, 19 },
};
   
static const nir_search_variable search118_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search118_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search118_0_0.value },
   NULL,
};

static const nir_search_variable search118_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search118 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search118_0.value, &search118_1.value },
   NULL,
};
   
static const nir_search_variable replace118_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace118_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression replace118 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace118_0.value, &replace118_1.value },
   NULL,
};
   
static const nir_search_variable search263_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search263_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search263_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search263_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search263_0_0.value, &search263_0_1.value, &search263_0_2.value },
   NULL,
};

static const nir_search_variable search263_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search263 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search263_0.value, &search263_1.value },
   NULL,
};
   
static const nir_search_variable replace263_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace263_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
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
   nir_op_fne,
   { &replace263_1_0.value, &replace263_1_1.value },
   NULL,
};

static const nir_search_variable replace263_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace263_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace263_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace263_2_0.value, &replace263_2_1.value },
   NULL,
};
static const nir_search_expression replace263 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace263_0.value, &replace263_1.value, &replace263_2.value },
   NULL,
};
   
static const nir_search_variable search264_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search264_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search264_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search264_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search264_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search264_1_0.value, &search264_1_1.value, &search264_1_2.value },
   NULL,
};
static const nir_search_expression search264 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &search264_0.value, &search264_1.value },
   NULL,
};
   
static const nir_search_variable replace264_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace264_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace264_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace264_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace264_1_0.value, &replace264_1_1.value },
   NULL,
};

static const nir_search_variable replace264_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace264_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace264_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace264_2_0.value, &replace264_2_1.value },
   NULL,
};
static const nir_search_expression replace264 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace264_0.value, &replace264_1.value, &replace264_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fne_xforms[] = {
   { &search118, &replace118.value, 0 },
   { &search263, &replace263.value, 0 },
   { &search264, &replace264.value, 0 },
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
   
static const nir_search_variable search200_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search200_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &search200_0_0.value },
   NULL,
};
static const nir_search_expression search200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search200_0.value },
   NULL,
};
   
static const nir_search_variable replace200_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace200 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace200_0.value },
   NULL,
};
   
static const nir_search_constant search221_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable search221_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search221_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &search221_0_0.value, &search221_0_1.value },
   NULL,
};
static const nir_search_expression search221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &search221_0.value },
   NULL,
};
   
static const nir_search_variable replace221_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace221 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iabs,
   { &replace221_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_iabs_xforms[] = {
   { &search15, &replace15.value, 0 },
   { &search16, &replace16.value, 0 },
   { &search200, &replace200.value, 0 },
   { &search221, &replace221.value, 0 },
};
   
static const nir_search_variable search245_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search245 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_unpack_unorm_4x8,
   { &search245_0.value },
   NULL,
};
   
static const nir_search_variable replace245_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace245_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};
static const nir_search_expression replace245_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace245_0_0_0_0.value, &replace245_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace245_0_0_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace245_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace245_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace245_0_0_1_0.value, &replace245_0_0_1_1.value },
   NULL,
};

static const nir_search_variable replace245_0_0_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace245_0_0_2_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x2 /* 2 */ },
};
static const nir_search_expression replace245_0_0_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace245_0_0_2_0.value, &replace245_0_0_2_1.value },
   NULL,
};

static const nir_search_variable replace245_0_0_3_0 = {
   { nir_search_value_variable, 0 },
   0, /* v */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace245_0_0_3_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};
static const nir_search_expression replace245_0_0_3 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_u8,
   { &replace245_0_0_3_0.value, &replace245_0_0_3_1.value },
   NULL,
};
static const nir_search_expression replace245_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_vec4,
   { &replace245_0_0_0.value, &replace245_0_0_1.value, &replace245_0_0_2.value, &replace245_0_0_3.value },
   NULL,
};
static const nir_search_expression replace245_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_u2f32,
   { &replace245_0_0.value },
   NULL,
};

static const nir_search_constant replace245_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x406fe00000000000 /* 255.0 */ },
};
static const nir_search_expression replace245 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdiv,
   { &replace245_0.value, &replace245_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_unpack_unorm_4x8_xforms[] = {
   { &search245, &replace245.value, 34 },
};
   
static const nir_search_variable search78_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search78_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search78 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search78_0.value, &search78_1.value },
   NULL,
};
   
static const nir_search_variable replace78 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search84_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search84_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search84_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search84_1_0.value },
   NULL,
};
static const nir_search_expression search84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search84_0.value, &search84_1.value },
   NULL,
};
   
static const nir_search_variable replace84_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace84_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &replace84_0_0.value },
   NULL,
};
static const nir_search_expression replace84 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace84_0.value },
   NULL,
};
   
static const nir_search_variable search86_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search86_1_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search86_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search86_1_0_0.value },
   NULL,
};
static const nir_search_expression search86_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search86_1_0.value },
   NULL,
};
static const nir_search_expression search86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
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
   nir_op_fabs,
   { &replace86_0_0.value },
   NULL,
};
static const nir_search_expression replace86 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace86_0.value },
   NULL,
};
   
static const nir_search_variable search88_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search88_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search88_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fabs,
   { &search88_1_0.value },
   NULL,
};
static const nir_search_expression search88 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
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
   
static const nir_search_variable search96_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant search96_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search96_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search96_0_0.value, &search96_0_1.value },
   NULL,
};

static const nir_search_constant search96_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression search96 = {
   { nir_search_value_expression, 0 },
   true,
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
static const nir_search_expression replace96 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace96_0.value },
   NULL,
};
   
static const nir_search_variable search100_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search100_0_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search100_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search100_0_0_0_0.value, &search100_0_0_0_1.value },
   NULL,
};

static const nir_search_variable search100_0_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search100_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search100_0_0_0.value, &search100_0_0_1.value },
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
   nir_op_fmax,
   { &search100_0_0.value, &search100_0_1.value },
   NULL,
};

static const nir_search_variable search100_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search100_0.value, &search100_1.value },
   NULL,
};
   
static const nir_search_variable replace100_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace100_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace100_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace100_0_0.value, &replace100_0_1.value },
   NULL,
};

static const nir_search_variable replace100_1 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace100 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace100_0.value, &replace100_1.value },
   NULL,
};
   
static const nir_search_variable search104_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search104_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &search104_0_0.value },
   NULL,
};

static const nir_search_variable search104_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   true,
   nir_type_invalid,
   (is_zero_to_one),
};
static const nir_search_expression search104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search104_0.value, &search104_1.value },
   NULL,
};
   
static const nir_search_variable replace104_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace104_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace104_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace104_0_0.value, &replace104_0_1.value },
   NULL,
};
static const nir_search_expression replace104 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fsat,
   { &replace104_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_fmin_xforms[] = {
   { &search78, &replace78.value, 0 },
   { &search84, &replace84.value, 0 },
   { &search86, &replace86.value, 0 },
   { &search88, &replace88.value, 0 },
   { &search96, &replace96.value, 9 },
   { &search100, &replace100.value, 0 },
   { &search104, &replace104.value, 0 },
};
   
static const nir_search_variable search130_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search130_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search130 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search130_0.value, &search130_1.value },
   NULL,
};
   
static const nir_search_constant replace130 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
   
static const nir_search_variable search180_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search180_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_TRUE /* True */ },
};
static const nir_search_expression search180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search180_0.value, &search180_1.value },
   (is_not_used_by_if),
};
   
static const nir_search_variable replace180_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace180 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &replace180_0.value },
   NULL,
};
   
static const nir_search_variable search181_0 = {
   { nir_search_value_variable, 32 },
   0, /* a */
   false,
   nir_type_bool32,
   NULL,
};

static const nir_search_constant search181_1 = {
   { nir_search_value_constant, 32 },
   nir_type_bool32, { NIR_FALSE /* False */ },
};
static const nir_search_expression search181 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search181_0.value, &search181_1.value },
   NULL,
};
   
static const nir_search_variable replace181 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
   
static const nir_search_variable search271_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search271_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search271_0_2 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search271_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search271_0_0.value, &search271_0_1.value, &search271_0_2.value },
   NULL,
};

static const nir_search_variable search271_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search271 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
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

static const nir_search_variable replace271_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace271_1_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace271_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace271_1_0.value, &replace271_1_1.value },
   NULL,
};

static const nir_search_variable replace271_2_0 = {
   { nir_search_value_variable, 0 },
   2, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace271_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* d */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace271_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace271_2_0.value, &replace271_2_1.value },
   NULL,
};
static const nir_search_expression replace271 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace271_0.value, &replace271_1.value, &replace271_2.value },
   NULL,
};
   
static const nir_search_variable search272_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search272_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search272_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search272_1_2 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   true,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search272_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &search272_1_0.value, &search272_1_1.value, &search272_1_2.value },
   NULL,
};
static const nir_search_expression search272 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &search272_0.value, &search272_1.value },
   NULL,
};
   
static const nir_search_variable replace272_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace272_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace272_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace272_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace272_1_0.value, &replace272_1_1.value },
   NULL,
};

static const nir_search_variable replace272_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* d */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace272_2_1 = {
   { nir_search_value_variable, 0 },
   3, /* c */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace272_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ine,
   { &replace272_2_0.value, &replace272_2_1.value },
   NULL,
};
static const nir_search_expression replace272 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace272_0.value, &replace272_1.value, &replace272_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ine_xforms[] = {
   { &search130, &replace130.value, 0 },
   { &search180, &replace180.value, 0 },
   { &search181, &replace181.value, 0 },
   { &search271, &replace271.value, 0 },
   { &search272, &replace272.value, 0 },
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
   
static const nir_search_variable search236_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search236_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i8,
   { &search236_0.value, &search236_1.value },
   NULL,
};
   
static const nir_search_variable replace236_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace236_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3 /* 3 */ },
};

static const nir_search_variable replace236_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace236_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace236_0_1_0_0.value, &replace236_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace236_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x8 /* 8 */ },
};
static const nir_search_expression replace236_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace236_0_1_0.value, &replace236_0_1_1.value },
   NULL,
};
static const nir_search_expression replace236_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace236_0_0.value, &replace236_0_1.value },
   NULL,
};

static const nir_search_constant replace236_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x18 /* 24 */ },
};
static const nir_search_expression replace236 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace236_0.value, &replace236_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_i8_xforms[] = {
   { &search236, &replace236.value, 27 },
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
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search231 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_uadd_carry,
   { &search231_0.value, &search231_1.value },
   NULL,
};
   
static const nir_search_variable replace231_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace231_0_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace231_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace231_0_0_0.value, &replace231_0_0_1.value },
   NULL,
};

static const nir_search_variable replace231_0_1 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace231_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ult,
   { &replace231_0_0.value, &replace231_0_1.value },
   NULL,
};
static const nir_search_expression replace231 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2i,
   { &replace231_0.value },
   NULL,
};

static const struct transform nir_opt_algebraic_uadd_carry_xforms[] = {
   { &search231, &replace231.value, 23 },
};
   
static const nir_search_variable search234_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search234_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search234_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibitfield_extract,
   { &search234_0.value, &search234_1.value, &search234_2.value },
   NULL,
};
   
static const nir_search_constant replace234_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1f /* 31 */ },
};

static const nir_search_variable replace234_0_1 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace234_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ilt,
   { &replace234_0_0.value, &replace234_0_1.value },
   NULL,
};

static const nir_search_variable replace234_1 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace234_2_0 = {
   { nir_search_value_variable, 0 },
   0, /* value */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace234_2_1 = {
   { nir_search_value_variable, 0 },
   1, /* offset */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace234_2_2 = {
   { nir_search_value_variable, 0 },
   2, /* bits */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace234_2 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ibfe,
   { &replace234_2_0.value, &replace234_2_1.value, &replace234_2_2.value },
   NULL,
};
static const nir_search_expression replace234 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace234_0.value, &replace234_1.value, &replace234_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ibitfield_extract_xforms[] = {
   { &search234, &replace234.value, 26 },
};
   
static const nir_search_variable search248_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search248_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search248 = {
   { nir_search_value_expression, 32 },
   false,
   nir_op_ldexp,
   { &search248_0.value, &search248_1.value },
   NULL,
};
   
static const nir_search_variable replace248_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace248_0_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace248_0_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace248_0_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace248_0_1_0_0_0_0_0.value, &replace248_0_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace248_0_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace248_0_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace248_0_1_0_0_0_0.value, &replace248_0_1_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace248_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace248_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace248_0_1_0_0_0.value, &replace248_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace248_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
static const nir_search_expression replace248_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace248_0_1_0_0.value, &replace248_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace248_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
static const nir_search_expression replace248_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace248_0_1_0.value, &replace248_0_1_1.value },
   NULL,
};
static const nir_search_expression replace248_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace248_0_0.value, &replace248_0_1.value },
   NULL,
};

static const nir_search_variable replace248_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace248_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace248_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace248_1_0_0_0_0_0.value, &replace248_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace248_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace248_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace248_1_0_0_0_0.value, &replace248_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace248_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace248_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0xfc /* -252 */ },
};
static const nir_search_expression replace248_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace248_1_0_0_1_0_0_0.value, &replace248_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace248_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0xfe /* 254 */ },
};
static const nir_search_expression replace248_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace248_1_0_0_1_0_0.value, &replace248_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace248_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace248_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace248_1_0_0_1_0.value, &replace248_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace248_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace248_1_0_0_0.value, &replace248_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace248_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7f /* 127 */ },
};
static const nir_search_expression replace248_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace248_1_0_0.value, &replace248_1_0_1.value },
   NULL,
};

static const nir_search_constant replace248_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x17 /* 23 */ },
};
static const nir_search_expression replace248_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace248_1_0.value, &replace248_1_1.value },
   NULL,
};
static const nir_search_expression replace248 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace248_0.value, &replace248_1.value },
   NULL,
};
   
static const nir_search_variable search249_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search249_1 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search249 = {
   { nir_search_value_expression, 64 },
   false,
   nir_op_ldexp,
   { &search249_0.value, &search249_1.value },
   NULL,
};
   
static const nir_search_variable replace249_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* x */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace249_0_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace249_0_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace249_0_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace249_0_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace249_0_1_1_0_0_0_0_0.value, &replace249_0_1_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace249_0_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace249_0_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace249_0_1_1_0_0_0_0.value, &replace249_0_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace249_0_1_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace249_0_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace249_0_1_1_0_0_0.value, &replace249_0_1_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace249_0_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
static const nir_search_expression replace249_0_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace249_0_1_1_0_0.value, &replace249_0_1_1_0_1.value },
   NULL,
};

static const nir_search_constant replace249_0_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
static const nir_search_expression replace249_0_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace249_0_1_1_0.value, &replace249_0_1_1_1.value },
   NULL,
};
static const nir_search_expression replace249_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &replace249_0_1_0.value, &replace249_0_1_1.value },
   NULL,
};
static const nir_search_expression replace249_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace249_0_0.value, &replace249_0_1.value },
   NULL,
};

static const nir_search_constant replace249_1_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x0 /* 0 */ },
};

static const nir_search_variable replace249_1_1_0_0_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace249_1_1_0_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace249_1_1_0_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace249_1_1_0_0_0_0_0.value, &replace249_1_1_0_0_0_0_1.value },
   NULL,
};

static const nir_search_constant replace249_1_1_0_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace249_1_1_0_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace249_1_1_0_0_0_0.value, &replace249_1_1_0_0_0_1.value },
   NULL,
};

static const nir_search_variable replace249_1_1_0_0_1_0_0_0 = {
   { nir_search_value_variable, 0 },
   1, /* exp */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace249_1_1_0_0_1_0_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { -0x7fc /* -2044 */ },
};
static const nir_search_expression replace249_1_1_0_0_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imax,
   { &replace249_1_1_0_0_1_0_0_0.value, &replace249_1_1_0_0_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace249_1_1_0_0_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x7fe /* 2046 */ },
};
static const nir_search_expression replace249_1_1_0_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imin,
   { &replace249_1_1_0_0_1_0_0.value, &replace249_1_1_0_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace249_1_1_0_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};
static const nir_search_expression replace249_1_1_0_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace249_1_1_0_0_1_0.value, &replace249_1_1_0_0_1_1.value },
   NULL,
};
static const nir_search_expression replace249_1_1_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace249_1_1_0_0_0.value, &replace249_1_1_0_0_1.value },
   NULL,
};

static const nir_search_constant replace249_1_1_0_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x3ff /* 1023 */ },
};
static const nir_search_expression replace249_1_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_iadd,
   { &replace249_1_1_0_0.value, &replace249_1_1_0_1.value },
   NULL,
};

static const nir_search_constant replace249_1_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x14 /* 20 */ },
};
static const nir_search_expression replace249_1_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace249_1_1_0.value, &replace249_1_1_1.value },
   NULL,
};
static const nir_search_expression replace249_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_pack_64_2x32_split,
   { &replace249_1_0.value, &replace249_1_1.value },
   NULL,
};
static const nir_search_expression replace249 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmul,
   { &replace249_0.value, &replace249_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_ldexp_xforms[] = {
   { &search248, &replace248.value, 0 },
   { &search249, &replace249.value, 0 },
};
   
static const nir_search_variable search238_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search238_1 = {
   { nir_search_value_variable, 32 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_extract_i16,
   { &search238_0.value, &search238_1.value },
   NULL,
};
   
static const nir_search_variable replace238_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace238_0_1_0_0 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x1 /* 1 */ },
};

static const nir_search_variable replace238_0_1_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace238_0_1_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_isub,
   { &replace238_0_1_0_0.value, &replace238_0_1_0_1.value },
   NULL,
};

static const nir_search_constant replace238_0_1_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace238_0_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_imul,
   { &replace238_0_1_0.value, &replace238_0_1_1.value },
   NULL,
};
static const nir_search_expression replace238_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishl,
   { &replace238_0_0.value, &replace238_0_1.value },
   NULL,
};

static const nir_search_constant replace238_1 = {
   { nir_search_value_constant, 0 },
   nir_type_int, { 0x10 /* 16 */ },
};
static const nir_search_expression replace238 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_ishr,
   { &replace238_0.value, &replace238_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_extract_i16_xforms[] = {
   { &search238, &replace238.value, 28 },
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
#include "nir_search_helpers.h"

#ifndef NIR_OPT_ALGEBRAIC_STRUCT_DEFS
#define NIR_OPT_ALGEBRAIC_STRUCT_DEFS

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

#endif

   
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
   nir_op_inot,
   { &search285_0_0.value },
   NULL,
};
static const nir_search_expression search285 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search285_0.value },
   (is_used_more_than_once),
};
   
static const nir_search_variable replace285_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace285_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};

static const nir_search_constant replace285_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x3ff0000000000000 /* 1.0 */ },
};
static const nir_search_expression replace285 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace285_0.value, &replace285_1.value, &replace285_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_b2f_xforms[] = {
   { &search285, &replace285.value, 0 },
};
   
static const nir_search_variable search283_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search283_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search283 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot4,
   { &search283_0.value, &search283_1.value },
   NULL,
};
   
static const nir_search_variable replace283_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace283_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace283 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated4,
   { &replace283_0.value, &replace283_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot4_xforms[] = {
   { &search283, &replace283.value, 37 },
};
   
static const nir_search_variable search277_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search277_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search277_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search277_0_0.value, &search277_0_1.value },
   NULL,
};

static const nir_search_constant search277_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search277 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &search277_0.value, &search277_1.value },
   NULL,
};
   
static const nir_search_variable replace277_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace277_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace277_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace277_1_0.value },
   NULL,
};
static const nir_search_expression replace277 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_flt,
   { &replace277_0.value, &replace277_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_flt_xforms[] = {
   { &search277, &replace277.value, 0 },
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
   nir_op_fdot2,
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

static const nir_search_variable replace281_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace281 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated2,
   { &replace281_0.value, &replace281_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot2_xforms[] = {
   { &search281, &replace281.value, 37 },
};
   
static const nir_search_variable search282_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search282_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search282 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot3,
   { &search282_0.value, &search282_1.value },
   NULL,
};
   
static const nir_search_variable replace282_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace282_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace282 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdot_replicated3,
   { &replace282_0.value, &replace282_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdot3_xforms[] = {
   { &search282, &replace282.value, 37 },
};
   
static const nir_search_variable search286_0_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search286_0_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_inot,
   { &search286_0_0_0.value },
   NULL,
};
static const nir_search_expression search286_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_b2f,
   { &search286_0_0.value },
   NULL,
};
static const nir_search_expression search286 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &search286_0.value },
   (is_used_more_than_once),
};
   
static const nir_search_variable replace286_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_constant replace286_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x8000000000000000L /* -0.0 */ },
};

static const nir_search_constant replace286_2 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0xbff0000000000000L /* -1.0 */ },
};
static const nir_search_expression replace286 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_bcsel,
   { &replace286_0.value, &replace286_1.value, &replace286_2.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fneg_xforms[] = {
   { &search286, &replace286.value, 0 },
};
   
static const nir_search_variable search288_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search288_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search288_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search288_0_0.value, &search288_0_1.value },
   (is_used_once),
};

static const nir_search_variable search288_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search288_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search288_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search288_1_0.value, &search288_1_1.value },
   (is_used_once),
};
static const nir_search_expression search288 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &search288_0.value, &search288_1.value },
   NULL,
};
   
static const nir_search_variable replace288_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace288_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace288_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace288_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmax,
   { &replace288_1_0.value, &replace288_1_1.value },
   NULL,
};
static const nir_search_expression replace288 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace288_0.value, &replace288_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fmax_xforms[] = {
   { &search288, &replace288.value, 0 },
};
   
static const nir_search_variable search280_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search280_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search280_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search280_0_0.value, &search280_0_1.value },
   NULL,
};

static const nir_search_constant search280_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search280 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fne,
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

static const nir_search_variable replace280_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace280_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace280_1_0.value },
   NULL,
};
static const nir_search_expression replace280 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fne,
   { &replace280_0.value, &replace280_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fne_xforms[] = {
   { &search280, &replace280.value, 0 },
};
   
static const nir_search_variable search287_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search287_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search287_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search287_0_0.value, &search287_0_1.value },
   (is_used_once),
};

static const nir_search_variable search287_1_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   true,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search287_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search287_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search287_1_0.value, &search287_1_1.value },
   (is_used_once),
};
static const nir_search_expression search287 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &search287_0.value, &search287_1.value },
   NULL,
};
   
static const nir_search_variable replace287_0 = {
   { nir_search_value_variable, 0 },
   0, /* c */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace287_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace287_1_1 = {
   { nir_search_value_variable, 0 },
   2, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace287_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fmin,
   { &replace287_1_0.value, &replace287_1_1.value },
   NULL,
};
static const nir_search_expression replace287 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &replace287_0.value, &replace287_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fmin_xforms[] = {
   { &search287, &replace287.value, 0 },
};
   
static const nir_search_variable search279_0_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search279_0_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search279_0 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fadd,
   { &search279_0_0.value, &search279_0_1.value },
   NULL,
};

static const nir_search_constant search279_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search279 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_feq,
   { &search279_0.value, &search279_1.value },
   NULL,
};
   
static const nir_search_variable replace279_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace279_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace279_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace279_1_0.value },
   NULL,
};
static const nir_search_expression replace279 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_feq,
   { &replace279_0.value, &replace279_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_feq_xforms[] = {
   { &search279, &replace279.value, 0 },
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
   nir_op_fadd,
   { &search278_0_0.value, &search278_0_1.value },
   NULL,
};

static const nir_search_constant search278_1 = {
   { nir_search_value_constant, 0 },
   nir_type_float, { 0x0 /* 0.0 */ },
};
static const nir_search_expression search278 = {
   { nir_search_value_expression, 0 },
   true,
   nir_op_fge,
   { &search278_0.value, &search278_1.value },
   NULL,
};
   
static const nir_search_variable replace278_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace278_1_0 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace278_1 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fneg,
   { &replace278_1_0.value },
   NULL,
};
static const nir_search_expression replace278 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fge,
   { &replace278_0.value, &replace278_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fge_xforms[] = {
   { &search278, &replace278.value, 0 },
};
   
static const nir_search_variable search284_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable search284_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression search284 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph,
   { &search284_0.value, &search284_1.value },
   NULL,
};
   
static const nir_search_variable replace284_0 = {
   { nir_search_value_variable, 0 },
   0, /* a */
   false,
   nir_type_invalid,
   NULL,
};

static const nir_search_variable replace284_1 = {
   { nir_search_value_variable, 0 },
   1, /* b */
   false,
   nir_type_invalid,
   NULL,
};
static const nir_search_expression replace284 = {
   { nir_search_value_expression, 0 },
   false,
   nir_op_fdph_replicated,
   { &replace284_0.value, &replace284_1.value },
   NULL,
};

static const struct transform nir_opt_algebraic_late_fdph_xforms[] = {
   { &search284, &replace284.value, 37 },
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

