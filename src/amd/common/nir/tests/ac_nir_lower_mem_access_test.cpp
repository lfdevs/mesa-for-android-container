/*
 * Copyright © 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include <gtest/gtest.h>

#include "util/string_buffer.h"
#include "ac_gpu_info.h"
#include "ac_nir.h"
#include "nir_builder.h"

struct subtest {
   amd_gfx_level gfx_level = GFX6;
   bool use_llvm = false;
   nir_intrinsic_op op = nir_num_intrinsics;
   unsigned num_components = 0;
   unsigned bit_size = 0;
   unsigned align_mul = 0;
   unsigned align_offset = 0;
   unsigned access = 0;
};

struct test_state {
   subtest *st = NULL;
   nir_shader *shader = NULL;
   nir_def *offset = NULL;
   unsigned num_result_intrins = 0;
   _mesa_string_buffer *result = NULL;
};

static bool format_offset(_mesa_string_buffer *str, nir_def *add, nir_def *src)
{
   nir_scalar s = nir_get_scalar(add, 0);

   if (s.def == src)
      return true;

   uint64_t imm;
   if (!nir_scalar_is_alu(s) ||
       (nir_scalar_alu_op(s) != nir_op_iand && nir_scalar_alu_op(s) != nir_op_iadd))
      return false;

   bool is_and = nir_scalar_alu_op(s) == nir_op_iand;
   nir_scalar src0 = nir_scalar_chase_alu_src(s, 0);
   nir_scalar src1 = nir_scalar_chase_alu_src(s, 1);
   if (nir_scalar_is_const(src0)) {
      imm = nir_scalar_as_uint(src0);
      s = src1;
   } else if (nir_scalar_is_const(src1)) {
      imm = nir_scalar_as_uint(src1);
      s = src0;
   } else {
      return false;
   }

   if (s.comp || !format_offset(str, s.def, src))
      return false;

   if (is_and)
      _mesa_string_buffer_printf(str, "&%" PRId64, (int64_t)util_sign_extend(imm, add->bit_size));
   else
      _mesa_string_buffer_printf(str, "%+" PRId64, (int64_t)util_sign_extend(imm, add->bit_size));

   return true;
}

static void format_intrinsic(_mesa_string_buffer *str, nir_intrinsic_instr *intrin, nir_def *offset,
                             bool print_access)
{
   unsigned num_components, bit_size;
   if (nir_intrinsic_infos[intrin->intrinsic].has_dest) {
      num_components = intrin->def.num_components;
      bit_size = intrin->def.bit_size;
   } else {
      num_components = intrin->src[0].ssa->num_components;
      bit_size = intrin->src[0].ssa->bit_size;
   }

   unsigned align_mul = nir_intrinsic_align_mul(intrin);
   unsigned align_offset = nir_intrinsic_align_offset(intrin);
   unsigned access = 0;
   if (nir_intrinsic_has_access(intrin))
      access = nir_intrinsic_access(intrin);

   if (align_mul == bit_size / 8 && align_offset == 0)
      align_mul = 0;

   _mesa_string_buffer_printf(str, "%ux%u(", bit_size, num_components);

   nir_def *new_offset = nir_get_io_offset_src(intrin)->ssa;
   _mesa_string_buffer *offset_str = _mesa_string_buffer_create(NULL, 0);
   if (new_offset != offset && format_offset(offset_str, new_offset, offset))
      _mesa_string_buffer_printf(str, "%s,", offset_str->buf);
   else if (new_offset != offset)
      _mesa_string_buffer_append(str, "unknown,");
   _mesa_string_buffer_destroy(offset_str);

   if (align_mul && align_offset)
      _mesa_string_buffer_printf(str, "align=%u,%u,", align_mul, align_offset);
   else if (align_mul)
      _mesa_string_buffer_printf(str, "align=%u,", align_mul);

   if (print_access && (access & ACCESS_SMEM_AMD))
      _mesa_string_buffer_append(str, "smem,");

   if (str->buf[str->length - 1] == ',')
      str->buf[--str->length] = '\0';
   if (str->buf[str->length - 1] == '(')
      str->buf[--str->length] = '\0';
   else
      _mesa_string_buffer_append_char(str, ')');
}

void create_shader(test_state *state, nir_shader_compiler_options *options)
{
   subtest *st = state->st;
   nir_builder _b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, NULL);
   nir_builder *b = &_b;
   state->shader = b->shader;

   unsigned offset_bit_size =
      st->op == nir_intrinsic_load_global || st->op == nir_intrinsic_store_global ? 64 : 32;
   nir_def *offset;
   if (st->access & ACCESS_SMEM_AMD)
      offset = nir_unit_test_uniform_amd(b, 1, offset_bit_size);
   else
      offset = nir_unit_test_divergent_amd(b, 1, offset_bit_size);
   state->offset = offset;

   b->shader->info.next_stage = MESA_SHADER_NONE;
   b->shader->info.internal = false;

   nir_def *def = NULL;
   nir_intrinsic_instr *instr = NULL;
   switch (st->op) {
   case nir_intrinsic_load_ssbo:
      def = nir_load_ssbo(b, st->num_components, st->bit_size, nir_imm_zero(b, 1, 32), offset);
      break;
   case nir_intrinsic_load_push_constant:
      def = nir_load_push_constant(b, st->num_components, st->bit_size, offset);
      break;
   case nir_intrinsic_load_scratch:
      def = nir_load_scratch(b, st->num_components, st->bit_size, offset);
      break;
   case nir_intrinsic_load_global:
      def = nir_load_global(b, st->num_components, st->bit_size, offset);
      break;
   case nir_intrinsic_load_shared:
      def = nir_load_shared(b, st->num_components, st->bit_size, offset);
      break;
   case nir_intrinsic_store_ssbo:
      instr = nir_store_ssbo(b, nir_undef(b, st->num_components, st->bit_size),
                             nir_imm_zero(b, 1, 32), offset);
      break;
   case nir_intrinsic_store_scratch:
      instr = nir_store_scratch(b, nir_undef(b, st->num_components, st->bit_size), offset);
      break;
   case nir_intrinsic_store_global:
      instr = nir_store_global(b, nir_undef(b, st->num_components, st->bit_size), offset);
      break;
   case nir_intrinsic_store_shared:
      instr = nir_store_shared(b, nir_undef(b, st->num_components, st->bit_size), offset);
      break;
   default:
      UNREACHABLE("");
   }
   if (def) {
      nir_use(b, def);
      instr = nir_def_as_intrinsic(def);
   }
   if (st->align_mul)
      nir_intrinsic_set_align(instr, st->align_mul, st->align_offset);
   if (nir_intrinsic_has_access(instr))
      nir_intrinsic_set_access(instr, (gl_access_qualifier)st->access);

   if (st->gfx_level != GFX11) {
      if (st->gfx_level >= GFX12)
         _mesa_string_buffer_printf(state->result, "gfx%u,", st->gfx_level - GFX12 + 12);
      else if (st->gfx_level == GFX11_5)
         _mesa_string_buffer_append(state->result, "gfx11.5,");
      else if (st->gfx_level >= GFX11)
         _mesa_string_buffer_printf(state->result, "gfx%u,", st->gfx_level - GFX11 + 11);
      else if (st->gfx_level == GFX10_3)
         _mesa_string_buffer_append(state->result, "gfx10.3,");
      else if (st->gfx_level >= GFX6)
         _mesa_string_buffer_printf(state->result, "gfx%u,", st->gfx_level - GFX6 + 6);
   }

   _mesa_string_buffer_printf(state->result, "%s: ", nir_intrinsic_infos[st->op].name);
   format_intrinsic(state->result, instr, offset, true);
   _mesa_string_buffer_append(state->result, " ->");
}

static bool count_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state_)
{
   test_state *state = (test_state *)state_;
   if (intrin->intrinsic == state->st->op)
      state->num_result_intrins++;
   return false;
}

static bool visit_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state_)
{
   test_state *state = (test_state *)state_;

   if (intrin->intrinsic != state->st->op)
      return false;

   const char *indent = state->num_result_intrins > 4 ? "\n   " : " ";
   _mesa_string_buffer_append(state->result, indent);
   format_intrinsic(state->result, intrin, state->offset, false);

   return false;
}

static void run_subtest(subtest *st, bool print = false)
{
   test_state state;
   state.st = st;
   state.result = _mesa_string_buffer_create(NULL, 0);

   struct radeon_info info = {};
   info.gfx_level = st->gfx_level;
   info.has_packed_math_16bit = true;
   info.has_accelerated_dot_product = true;

   nir_shader_compiler_options options = {};
   ac_nir_set_options(&info, st->use_llvm, &options);

   create_shader(&state, &options);

   unsigned printed;
   if (print) {
      printf("%s", state.result->buf);
      /* flush in case of crash during lowering */
      fflush(stdout);
      printed = state.result->length;
   }

   ac_nir_lower_mem_access_bit_sizes(state.shader, st->gfx_level, st->use_llvm);

   nir_shader_intrinsics_pass(state.shader, &count_intrinsic, nir_metadata_all, &state);
   nir_shader_intrinsics_pass(state.shader, &visit_intrinsic, nir_metadata_all, &state);
   _mesa_string_buffer_append_char(state.result, '\n');

   ralloc_free(state.shader);

   if (print)
      printf("%s", state.result->buf + printed);

   _mesa_string_buffer_destroy(state.result);
}

class lower_mem_access_test : public ::testing::Test {
 protected:
   lower_mem_access_test()
   {
      glsl_type_singleton_init_or_ref();
   }

   ~lower_mem_access_test()
   {
      glsl_type_singleton_decref();
   }

   void run_subtests(amd_gfx_level gfx_level, nir_intrinsic_op op, unsigned access = 0)
   {
      subtest st;
      st.gfx_level = gfx_level;
      st.use_llvm = false;
      st.op = op;
      st.access = access;
      for (st.bit_size = 8; st.bit_size <= 64; st.bit_size *= 2) {
         for (st.num_components = 0; st.num_components <= NIR_MAX_VEC_COMPONENTS;
              st.num_components++) {
            if (!nir_num_components_valid(st.num_components))
               continue;

            for (st.align_mul = 1; st.align_mul <= st.bit_size / 8; st.align_mul *= 2) {
               for (st.align_offset = 0; st.align_offset < st.align_mul; st.align_offset++) {
                  run_subtest(&st, print);
               }
            }
         }
      }
   }

   /* Replace this with true to verify ac_nir_lower_mem_access_bit_sizes changes. */
   bool print = false;
};

TEST_F(lower_mem_access_test, all)
{
   run_subtests(GFX11, nir_intrinsic_load_ssbo);
   run_subtests(GFX11, nir_intrinsic_load_ssbo, ACCESS_SMEM_AMD);
   run_subtests(GFX12, nir_intrinsic_load_ssbo, ACCESS_SMEM_AMD);
   run_subtests(GFX11, nir_intrinsic_load_push_constant);
   run_subtests(GFX11, nir_intrinsic_load_global);
   run_subtests(GFX11, nir_intrinsic_load_global, ACCESS_SMEM_AMD);
   run_subtests(GFX12, nir_intrinsic_load_global, ACCESS_SMEM_AMD);
   run_subtests(GFX11, nir_intrinsic_load_shared);
   run_subtests(GFX11, nir_intrinsic_load_scratch);
   run_subtests(GFX11, nir_intrinsic_store_ssbo);
   run_subtests(GFX11, nir_intrinsic_store_global);
   run_subtests(GFX11, nir_intrinsic_store_shared);
   run_subtests(GFX6, nir_intrinsic_store_shared);
   run_subtests(GFX11, nir_intrinsic_store_scratch);
}
