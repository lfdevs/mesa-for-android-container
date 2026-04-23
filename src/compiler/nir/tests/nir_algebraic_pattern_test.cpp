/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir_algebraic_pattern_test.h"
#include "nir_builder.h"
#include "nir_constant_expressions.h"
#include "nir_range_analysis.h"

#include "util/compiler.h"
#include "util/memstream.h"

#include <float.h>
#include <math.h>

nir_algebraic_pattern_test::nir_algebraic_pattern_test(const char *name)
    : nir_test(name)
{
   b->fp_math_ctrl = nir_fp_no_fast_math;
}

nir_const_value *
nir_algebraic_pattern_test::tmp_value(nir_def *def)
{
   return &tmp_values[def->index * NIR_MAX_VEC_COMPONENTS];
}

static bool
def_annotate_value(nir_def *def, void *data)
{
   nir_algebraic_pattern_test *test = (nir_algebraic_pattern_test *)data;

   char *annotation = NULL;
   size_t annotation_size = 0;
   u_memstream mem;
   if (!u_memstream_open(&mem, &annotation, &annotation_size))
      return true;

   FILE *output = u_memstream_get(&mem);

   nir_const_value *value = test->tmp_value(def);

   fprintf(output, "// ");
   if (def->num_components == 1) {
      fprintf(output, "0x%0" PRIx64, nir_const_value_as_uint(value[0], def->bit_size));
      if (def->bit_size >= 16)
         fprintf(output, " = %f", nir_const_value_as_float(value[0], def->bit_size));

   } else {
      fprintf(output, "(");
      for (uint32_t comp = 0; comp < def->num_components; comp++) {
         if (comp > 0)
            fprintf(output, ", ");
         fprintf(output, "0x%0" PRIx64, nir_const_value_as_uint(value[comp], def->bit_size));
      }
      fprintf(output, ")");
      if (def->bit_size >= 16) {
         fprintf(output, " = (");
         for (uint32_t comp = 0; comp < def->num_components; comp++) {
            if (comp > 0)
               fprintf(output, ", ");
            fprintf(output, "%f", nir_const_value_as_float(value[comp], def->bit_size));
         }
         fprintf(output, ")");
      }
   }

   fputc(0, output);

   u_memstream_close(&mem);

   _mesa_hash_table_insert(test->annotations, nir_def_instr(def), annotation);

   return true;
}

static bool
instr_annotate_value(nir_builder *b, nir_instr *instr, void *data)
{
   nir_foreach_def(instr, def_annotate_value, data);
   return false;
}

nir_algebraic_pattern_test::~nir_algebraic_pattern_test()
{
   if (HasFailure()) {
      annotations = _mesa_pointer_hash_table_create(nullptr);
      nir_shader_instructions_pass(b->shader, instr_annotate_value, nir_metadata_all, this);
   }
}

static bool
nir_def_is_used_as(nir_def *def, nir_alu_type type)
{
   nir_foreach_use(use, def) {
      nir_instr *use_instr = nir_src_parent_instr(use);
      if (use_instr->type != nir_instr_type_alu)
         continue;

      /* This is the ALU instruction that contains the use, not
       * nir_src_as_alu()'s ALU instruction that generated the use's def.
       */
      nir_alu_instr *use_alu = nir_instr_as_alu(use_instr);

      uint32_t i = container_of(use, nir_alu_src, src) - use_alu->src;
      assert(i < nir_op_infos[use_alu->op].num_inputs);

      if (nir_alu_type_get_base_type(nir_op_infos[use_alu->op].input_types[i]) == type)
         return true;

      if (nir_op_is_vec_or_mov(use_alu->op) && nir_def_is_used_as(&use_alu->def, type))
         return true;
   }

   return false;
}

#define INPUT_VALUE_COUNT_LOG2 3
#define INPUT_VALUE_COUNT      (1 << INPUT_VALUE_COUNT_LOG2)
#define INPUT_VALUE_MASK       ((1 << INPUT_VALUE_COUNT_LOG2) - 1)

static uint32_t
get_seed_bit_size(input_type type)
{
   if (type == BOOL)
      return 1;
   else
      return INPUT_VALUE_COUNT_LOG2;
}

/**
 * Rewrites the nir_intrinsic_unit_test_uniform_inputs to load_consts, and
 * tracks them as variables that need to be rewritten with the test inputs per
 * iteration.
 *
 * We can't emit load_consts directly in test generation, because nir_builder
 * helpers might try to fold those constants.
 */
static bool
map_input(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   nir_algebraic_pattern_test *test = (nir_algebraic_pattern_test *)data;

   if (intr->intrinsic != nir_intrinsic_unit_test_uniform_input)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *load = nir_imm_zero(b, intr->def.num_components, intr->def.bit_size);

   enum input_type ty;

   if (nir_def_is_used_as(&intr->def, nir_type_bool))
      ty = BOOL;
   else if (nir_def_is_used_as(&intr->def, nir_type_float))
      ty = FLOAT;
   else if (nir_def_is_used_as(&intr->def, nir_type_uint))
      ty = UINT;
   else
      ty = INT;
   test->inputs.push_back(nir_algebraic_pattern_test_input(nir_instr_as_load_const(nir_def_instr(load)),
                                                           ty, test->fuzzing_bits));
   test->fuzzing_bits += get_seed_bit_size(ty) * intr->def.num_components;

   nir_def_replace(&intr->def, load);

   return true;
}

static const uint64_t uint_inputs[INPUT_VALUE_COUNT] = {
   0,
   1,
   2,
   3,
   4,
   32,
   64,
   UINT64_MAX,
};

static const int64_t int_inputs[INPUT_VALUE_COUNT] = {
   0,
   1,
   -1,
   2,
   3,
   64,
   INT64_MIN,
   INT64_MAX,
};

static const double float_inputs[INPUT_VALUE_COUNT] = {
   0,
   1,
   -1,
   0.12345,
   NAN,
   INFINITY,
   -INFINITY,
   DBL_MIN,
};

void
nir_algebraic_pattern_test::handle_signed_zero(nir_const_value *val, uint32_t bit_size)
{
   if (bit_size < 16 || nir_const_value_as_float(*val, bit_size) != 0.0)
      return;

   /* If we're preserving signed zeroes, no need to do any of this work. */
   if (exact && (fp_math_ctrl & nir_fp_preserve_signed_zero))
      return;

   if (signed_zero_iter != 0) {
      if (signed_zero_count < 32 &&
          (1u << signed_zero_count) & signed_zero_iter) {
         switch (bit_size) {
         case 16:
            val->u16 ^= 0x8000;
            break;
         case 32:
            val->f32 = -val->f32;
            break;
         case 64:
            val->f64 = -val->f64;
            break;
         default:
            UNREACHABLE("bad bit size");
         }
      }
   }
   signed_zero_count++;
}

bool
nir_algebraic_pattern_test::skip_test(nir_alu_instr *alu, uint32_t bit_size,
                                      nir_const_value tmp, int32_t src_index)
{
   /* Always pass the test for signed zero/nan/inf sources if they are not preserved. */
   const nir_op_info *info = &nir_op_infos[alu->op];
   nir_alu_type type = src_index >= 0 ? info->input_types[src_index] : info->output_type;
   if (bit_size >= 16 && nir_alu_type_get_base_type(type) == nir_type_float) {
      double val = nir_const_value_as_float(tmp, bit_size);
      unsigned ctrl = fp_math_ctrl | ~info->valid_fp_math_ctrl;
      if ((!exact || !(ctrl & nir_fp_preserve_nan)) && isnan(val))
         return true;
      if ((!exact || !(ctrl & nir_fp_preserve_inf)) && isinf(val))
         return true;
   }

   switch (alu->op) {
   case nir_op_fsign:
      /* SPIRV's FSign says "If x = ±NaN, the result can be any of ±1.0 or ±0.0,
       * regardless of whether shader_float_controls is in use."
       */
      if (isnan(nir_const_value_as_float(tmp, bit_size)))
         return true;
      break;

   case nir_op_f2u8:
   case nir_op_f2u16:
   case nir_op_f2u32:
   case nir_op_f2i8:
   case nir_op_f2i16:
   case nir_op_f2i32:
      if (src_index == 0) {
         double value = nir_const_value_as_float(tmp, bit_size);
         /* Not called out in SPIRV, but a source of UB in our constant evaluation for sure. */
         if (isnan(value))
            return true;
      }
      break;

   default:
      break;
   }

   return false;
}

static bool
compare_inexact(double a, double b, uint32_t bit_size)
{
   return abs(a - b) > pow(0.5, bit_size / 4);
}

/* Returns true if this expression means the testcase passed with these input values
 * (either assert_eq was true, or we hit some UB with these inputs and the test should
 * be skipped).
 */
bool
nir_algebraic_pattern_test::evaluate_expression(nir_instr *instr)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

      if (intrinsic->intrinsic == nir_intrinsic_unit_test_assert_eq) {
         nir_const_value *src0 = tmp_value(intrinsic->src[0].ssa);
         nir_const_value *src1 = tmp_value(intrinsic->src[1].ssa);

         assert(intrinsic->src[0].ssa->bit_size == intrinsic->src[1].ssa->bit_size);
         uint32_t bit_size = intrinsic->src[0].ssa->bit_size;

         /* Note: fdot*_replicates replacements generate more channels than the
          * original pattern, but we care that the usable channels of the search
          * expression match.
          */
         assert(intrinsic->src[0].ssa->num_components == intrinsic->src[1].ssa->num_components);
         uint32_t num_components = intrinsic->src[0].ssa->num_components;

         nir_alu_instr *alu0 = nir_src_as_alu(intrinsic->src[0]);
         nir_alu_instr *alu1 = nir_src_as_alu(intrinsic->src[1]);
         bool is_float = (alu0 && nir_alu_type_get_base_type(nir_op_infos[alu0->op].output_type) == nir_type_float) ||
                         (alu1 && nir_alu_type_get_base_type(nir_op_infos[alu1->op].output_type) == nir_type_float);

         for (uint32_t comp = 0; comp < num_components; comp++) {
            uint64_t au = nir_const_value_as_uint(src0[comp], bit_size);
            uint64_t bu = nir_const_value_as_uint(src1[comp], bit_size);

            if (au != bu) {
               if (bit_size >= 16) {
                  double af = nir_const_value_as_float(src0[comp], bit_size);
                  double bf = nir_const_value_as_float(src1[comp], bit_size);
                  if (exact) {
                     if (!(is_float && isnan(af) && isnan(bf)))
                        return false;
                  } else {
                     /* NOTE: we do inexact float compare even on integer
                      * outputs!  This handles (poorly) the expected inexactness
                      * of e.g. the pack_half_2x16_split ->
                      * pack_half_2x16_rtz_split transform.
                      */
                     if (compare_inexact(af, bf, bit_size))
                        return false;
                  }
               } else {
                  return false;
               }
            }
         }

         return true;
      }

      return false;
   }

   if (instr->type == nir_instr_type_load_const) {
      nir_load_const_instr *load_const = nir_instr_as_load_const(instr);

      for (uint32_t i = 0; i < load_const->def.num_components; i++)
         tmp_value(&load_const->def)[i] = load_const->value[i];

      return false;
   }

   nir_alu_instr *alu = nir_instr_as_alu(instr);

   uint32_t bit_size = 0;
   if (!nir_alu_type_get_type_size(nir_op_infos[alu->op].output_type))
      bit_size = alu->def.bit_size;

   nir_const_value src[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
   for (uint32_t i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      if (bit_size == 0 &&
          !nir_alu_type_get_type_size(nir_op_infos[alu->op].input_types[i]))
         bit_size = alu->src[i].src.ssa->bit_size;

      for (uint32_t j = 0; j < nir_ssa_alu_instr_src_components(alu, i); j++) {
         nir_const_value tmp = tmp_value(alu->src[i].src.ssa)[alu->src[i].swizzle[j]];
         if (nir_alu_type_get_base_type(nir_op_infos[alu->op].input_types[i]) == nir_type_float)
            handle_signed_zero(&tmp, alu->src[i].src.ssa->bit_size);

         src[i][j] = tmp;
         if (skip_test(alu, alu->src[i].src.ssa->bit_size, tmp, i))
            return true;
      }
   }

   if (bit_size == 0)
      bit_size = 32;

   nir_const_value *srcs[NIR_MAX_VEC_COMPONENTS];
   for (uint32_t i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
      srcs[i] = src[i];

   nir_const_value *dest = tmp_value(&alu->def);

   nir_component_mask_t poison;
   nir_eval_const_opcode(alu->op, dest, &poison, alu->def.num_components, bit_size, srcs, b->shader->info.float_controls_execution_mode);

   /* If the inputs we chose triggered UB, then skip this particular test
    * combination -- we can't assert equality of the results (and we don't have
    * the UB of NIR opcodes well enough enumerated that we can assert that UB
    * was preserved by our transformations).
    */
   if (poison)
      return true;

   for (uint32_t comp = 0; comp < alu->def.num_components; comp++) {
      if (nir_alu_type_get_base_type(nir_op_infos[alu->op].output_type) == nir_type_float)
         handle_signed_zero(&dest[comp], alu->def.bit_size);
      if (skip_test(alu, bit_size, dest[comp], -1))
         return true;
   }

   return false;
}

/** Sets the load_const values that serve as the inputs to a test iteration. */
void
nir_algebraic_pattern_test::set_inputs(uint32_t seed)
{
   for (auto input : inputs) {
      nir_load_const_instr *load = input.instr;
      uint32_t seed_bit_size = get_seed_bit_size(input.ty);

      for (uint32_t comp = 0; comp < load->def.num_components; comp++) {
         uint32_t val = seed >> ((input.fuzzing_start_bit + seed_bit_size * comp) % 32);

         if (load->def.bit_size == 1) {
            load->value[comp].b = val & 1;
            continue;
         }

         /* Zero out the rest of the field. */
         load->value[comp].u64 = 0;

         switch (input.ty) {
         case BOOL:
            /* Single path here sets the bit pattern for any size of bool. */
            load->value[comp].u64 = (val & 1) ? ~0llu : 0;
            break;
         case FLOAT:
            load->value[comp] = nir_const_value_for_float(float_inputs[val & INPUT_VALUE_MASK],
                                                          load->def.bit_size);
            break;
         case UINT:
            load->value[comp] = nir_const_value_for_raw_uint(uint_inputs[val & INPUT_VALUE_MASK],
                                                             load->def.bit_size);
            break;
         case INT:
            load->value[comp] = nir_const_value_for_raw_uint(int_inputs[val & INPUT_VALUE_MASK],
                                                             load->def.bit_size);
            break;
         }
      }
   }
}

bool
nir_algebraic_pattern_test::check_variable_conds()
{
   if (variable_conds.empty())
      return true;

   nir_fp_analysis_state range_ht = nir_create_fp_analysis_state(b->impl);
   nir_search_state state = {
      .range_ht = &range_ht,
      .numlsb_ht = _mesa_pointer_hash_table_create(NULL),
   };

   bool result = true;
   for (auto cond : variable_conds) {
      uint8_t swiz[NIR_MAX_VEC_COMPONENTS];
      int num_components = nir_ssa_alu_instr_src_components(cond.alu, cond.src_index);
      for (int i = 0; i < num_components; i++)
         swiz[i] = i;
      if (!cond.cond(&state, cond.alu, cond.src_index, num_components, swiz)) {
         result = false;
         break;
      }
   }

   _mesa_hash_table_destroy(state.numlsb_ht, NULL);
   nir_free_fp_analysis_state(state.range_ht);

   return result;
}

void
nir_algebraic_pattern_test::validate_pattern()
{
   fuzzing_bits = 0;

   nir_function_impl *impl = nir_shader_get_entrypoint(b->shader);

   nir_validate_shader(b->shader, "validate_pattern");

   nir_shader_intrinsics_pass(b->shader, map_input, nir_metadata_all, this);

   nir_index_ssa_defs(impl);
   /* Write an obvious dummy value -- In the event that all inputs are
    * unexpectedly skipped, dummy values will show up in annotation after the
    * skip point.
    */
   tmp_values.assign(NIR_MAX_VEC_COMPONENTS * impl->ssa_alloc, nir_const_value{ .u64 = 0xd0d0d0d0d0d0d0d0 });

   if (expected_result != UNSUPPORTED) {
      ASSERT_EQ(expression_cond_failed, (char *)NULL);
   } else if (expression_cond_failed) {
      return;
   }

   enum result result = UNSUPPORTED; /* Default state if all input values get skipped */

   bool overflow = fuzzing_bits > 16;
   if (overflow)
      fuzzing_bits = 16;

   nir_block *block = nir_impl_last_block(impl);

   uint32_t iterations = 1 << fuzzing_bits;
   for (uint32_t i = 0; i < iterations; i++) {
      uint32_t seed;
      if (overflow) {
         /* Make sure we have the same test inputs on big-endian, since the hash
          * is byte-wise.
          */
         uint32_t hash_input = CPU_TO_LE32(i);
         seed = _mesa_hash_u32(&hash_input);
      } else {
         seed = i;
      }

      set_inputs(seed);
      if (!check_variable_conds())
         continue;

      /* Loop over the set of 0.0 sign flips we want to try to see if the
       * pattern works that way, given the NIR spec for
       * !nir_fp_preserve_signed_zero of "any -0.0 or +0.0 output can have
       * either sign, and any zero input can be treated as having opposite sign.
       */
      uint32_t saved_signed_zero_count = 0;
      enum result seed_result = UNSUPPORTED;
      for (signed_zero_iter = 0; signed_zero_iter < 1u << MIN2(4, saved_signed_zero_count); signed_zero_iter++) {
         /* This will get incremented as we evaluate the instrs. */
         signed_zero_count = 0;

         /* Loop over the instructions evaluating them given the inputs and this set of signed zero flips. */
         nir_foreach_instr(instr, block) {
            bool is_assert = (instr->type == nir_instr_type_intrinsic &&
                              nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_unit_test_assert_eq);

            if (evaluate_expression(instr)) {
               if (is_assert)
                  seed_result = PASS;
               break;
            } else {
               if (is_assert) {
                  seed_result = FAIL;
               }
            }
         }

         if (signed_zero_iter == 0)
            saved_signed_zero_count = signed_zero_count;

         if (seed_result == PASS)
            break;
      }

      if (seed_result == PASS) {
         result = PASS;
         /* Don't break out of the loop that feeds us new inputs -- we want to continue to test the rest to find a failure. */
      } else if (seed_result == UNSUPPORTED) {
         /* The test skipped for these inputs, don't change the final result. */
      } else {
         bool sz_non_exhaustive = saved_signed_zero_count > 31 || signed_zero_iter < (1u << saved_signed_zero_count);
         if (sz_non_exhaustive) {
            /* We don't seem to trigger this case in practice. */
            printf("Skipping test input due to too many signed zeroes to exhaustively test.\n");
         } else {
            /* We got a fail result with every combination of
             * nir_fp_preserve_signed_zero bit flips applied. Break out so we
             * can print the shader with the failing values.
             */
            result = FAIL;
            break;
         }
      }
   }

   ASSERT_EQ(result, expected_result);
}
