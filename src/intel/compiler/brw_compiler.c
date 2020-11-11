/*
 * Copyright © 2015-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_compiler.h"
#include "brw_shader.h"
#include "brw_eu.h"
#include "dev/gen_debug.h"
#include "compiler/nir/nir.h"
#include "main/errors.h"
#include "util/debug.h"

#define COMMON_OPTIONS                                                        \
   .lower_sub = true,                                                         \
   .lower_fdiv = true,                                                        \
   .lower_scmp = true,                                                        \
   .lower_flrp16 = true,                                                      \
   .lower_fmod = true,                                                        \
   .lower_bitfield_extract = true,                                            \
   .lower_bitfield_insert = true,                                             \
   .lower_uadd_carry = true,                                                  \
   .lower_usub_borrow = true,                                                 \
   .lower_fdiv = true,                                                        \
   .lower_flrp64 = true,                                                      \
   .lower_isign = true,                                                       \
   .lower_ldexp = true,                                                       \
   .lower_device_index_to_zero = true,                                        \
   .vectorize_io = true,                                                      \
   .use_interpolated_input_intrinsics = true,                                 \
   .vertex_id_zero_based = true,                                              \
   .lower_base_vertex = true,                                                 \
   .use_scoped_barrier = true,                                                \
   .support_16bit_alu = true,                                                 \
   .lower_uniforms_to_ubo = true

#define COMMON_SCALAR_OPTIONS                                                 \
   .lower_to_scalar = true,                                                   \
   .lower_pack_half_2x16 = true,                                              \
   .lower_pack_snorm_2x16 = true,                                             \
   .lower_pack_snorm_4x8 = true,                                              \
   .lower_pack_unorm_2x16 = true,                                             \
   .lower_pack_unorm_4x8 = true,                                              \
   .lower_unpack_half_2x16 = true,                                            \
   .lower_unpack_snorm_2x16 = true,                                           \
   .lower_unpack_snorm_4x8 = true,                                            \
   .lower_unpack_unorm_2x16 = true,                                           \
   .lower_unpack_unorm_4x8 = true,                                            \
   .lower_usub_sat64 = true,                                                  \
   .lower_hadd64 = true,                                                      \
   .lower_bfe_with_two_constants = true,                                      \
   .max_unroll_iterations = 32

static const struct nir_shader_compiler_options scalar_nir_options = {
   COMMON_OPTIONS,
   COMMON_SCALAR_OPTIONS,
};

static const struct nir_shader_compiler_options vector_nir_options = {
   COMMON_OPTIONS,

   /* In the vec4 backend, our dpN instruction replicates its result to all the
    * components of a vec4.  We would like NIR to give us replicated fdot
    * instructions because it can optimize better for us.
    */
   .fdot_replicates = true,

   .lower_pack_snorm_2x16 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .intel_vec4 = true,
   .max_unroll_iterations = 32,
};

struct brw_compiler *
brw_compiler_create(void *mem_ctx, const struct gen_device_info *devinfo)
{
   struct brw_compiler *compiler = rzalloc(mem_ctx, struct brw_compiler);

   compiler->devinfo = devinfo;

   brw_fs_alloc_reg_sets(compiler);
   brw_vec4_alloc_reg_set(compiler);

   compiler->precise_trig = env_var_as_boolean("INTEL_PRECISE_TRIG", false);

   compiler->use_tcs_8_patch =
      devinfo->gen >= 12 ||
      (devinfo->gen >= 9 && (INTEL_DEBUG & DEBUG_TCS_EIGHT_PATCH));

   /* Default to the sampler since that's what we've done since forever */
   compiler->indirect_ubos_use_sampler = true;

   /* There is no vec4 mode on Gen10+, and we don't use it at all on Gen8+. */
   for (int i = MESA_SHADER_VERTEX; i < MESA_ALL_SHADER_STAGES; i++) {
      compiler->scalar_stage[i] = devinfo->gen >= 8 ||
         i == MESA_SHADER_FRAGMENT || i == MESA_SHADER_COMPUTE;
   }

   nir_lower_int64_options int64_options =
      nir_lower_imul64 |
      nir_lower_isign64 |
      nir_lower_divmod64 |
      nir_lower_imul_high64;
   nir_lower_doubles_options fp64_options =
      nir_lower_drcp |
      nir_lower_dsqrt |
      nir_lower_drsq |
      nir_lower_dtrunc |
      nir_lower_dfloor |
      nir_lower_dceil |
      nir_lower_dfract |
      nir_lower_dround_even |
      nir_lower_dmod |
      nir_lower_dsub |
      nir_lower_ddiv;

   if (!devinfo->has_64bit_float || (INTEL_DEBUG & DEBUG_SOFT64)) {
      int64_options |= (nir_lower_int64_options)~0;
      fp64_options |= nir_lower_fp64_full_software;
   }

   /* The Bspec's section tittled "Instruction_multiply[DevBDW+]" claims that
    * destination type can be Quadword and source type Doubleword for Gen8 and
    * Gen9. So, lower 64 bit multiply instruction on rest of the platforms.
    */
   if (devinfo->gen < 8 || devinfo->gen > 9)
      int64_options |= nir_lower_imul_2x32_64;

   /* We want the GLSL compiler to emit code that uses condition codes */
   for (int i = 0; i < MESA_ALL_SHADER_STAGES; i++) {
      compiler->glsl_compiler_options[i].MaxUnrollIterations = 0;
      compiler->glsl_compiler_options[i].MaxIfDepth =
         devinfo->gen < 6 ? 16 : UINT_MAX;

      /* We handle this in NIR */
      compiler->glsl_compiler_options[i].EmitNoIndirectInput = false;
      compiler->glsl_compiler_options[i].EmitNoIndirectOutput = false;
      compiler->glsl_compiler_options[i].EmitNoIndirectUniform = false;
      compiler->glsl_compiler_options[i].EmitNoIndirectTemp = false;

      bool is_scalar = compiler->scalar_stage[i];
      compiler->glsl_compiler_options[i].OptimizeForAOS = !is_scalar;

      struct nir_shader_compiler_options *nir_options =
         rzalloc(compiler, struct nir_shader_compiler_options);
      if (is_scalar) {
         *nir_options = scalar_nir_options;
      } else {
         *nir_options = vector_nir_options;
      }

      /* Prior to Gen6, there are no three source operations, and Gen11 loses
       * LRP.
       */
      nir_options->lower_ffma16 = devinfo->gen < 6;
      nir_options->lower_ffma32 = devinfo->gen < 6;
      nir_options->lower_ffma64 = devinfo->gen < 6;
      nir_options->lower_flrp32 = devinfo->gen < 6 || devinfo->gen >= 11;
      nir_options->lower_fpow = devinfo->gen >= 12;

      nir_options->lower_rotate = devinfo->gen < 11;
      nir_options->lower_bitfield_reverse = devinfo->gen < 7;

      nir_options->lower_int64_options = int64_options;
      nir_options->lower_doubles_options = fp64_options;

      /* Starting with Gen11, we lower away 8-bit arithmetic */
      nir_options->support_8bit_alu = devinfo->gen < 11;

      nir_options->unify_interfaces = i < MESA_SHADER_FRAGMENT;

      compiler->glsl_compiler_options[i].NirOptions = nir_options;

      compiler->glsl_compiler_options[i].ClampBlockIndicesToArrayBounds = true;
   }

   return compiler;
}

static void
insert_u64_bit(uint64_t *val, bool add)
{
   *val = (*val << 1) | !!add;
}

uint64_t
brw_get_compiler_config_value(const struct brw_compiler *compiler)
{
   uint64_t config = 0;
   insert_u64_bit(&config, compiler->precise_trig);
   if (compiler->devinfo->gen >= 8 && compiler->devinfo->gen < 10) {
      insert_u64_bit(&config, compiler->scalar_stage[MESA_SHADER_VERTEX]);
      insert_u64_bit(&config, compiler->scalar_stage[MESA_SHADER_TESS_CTRL]);
      insert_u64_bit(&config, compiler->scalar_stage[MESA_SHADER_TESS_EVAL]);
      insert_u64_bit(&config, compiler->scalar_stage[MESA_SHADER_GEOMETRY]);
   }
   uint64_t debug_bits = INTEL_DEBUG;
   uint64_t mask = DEBUG_DISK_CACHE_MASK;
   while (mask != 0) {
      const uint64_t bit = 1ULL << (ffsll(mask) - 1);
      insert_u64_bit(&config, (debug_bits & bit) != 0);
      mask &= ~bit;
   }
   return config;
}

unsigned
brw_prog_data_size(gl_shader_stage stage)
{
   static const size_t stage_sizes[] = {
      [MESA_SHADER_VERTEX]    = sizeof(struct brw_vs_prog_data),
      [MESA_SHADER_TESS_CTRL] = sizeof(struct brw_tcs_prog_data),
      [MESA_SHADER_TESS_EVAL] = sizeof(struct brw_tes_prog_data),
      [MESA_SHADER_GEOMETRY]  = sizeof(struct brw_gs_prog_data),
      [MESA_SHADER_FRAGMENT]  = sizeof(struct brw_wm_prog_data),
      [MESA_SHADER_COMPUTE]   = sizeof(struct brw_cs_prog_data),
      [MESA_SHADER_KERNEL]    = sizeof(struct brw_cs_prog_data),
   };
   assert((int)stage >= 0 && stage < ARRAY_SIZE(stage_sizes));
   return stage_sizes[stage];
}

unsigned
brw_prog_key_size(gl_shader_stage stage)
{
   static const size_t stage_sizes[] = {
      [MESA_SHADER_VERTEX]    = sizeof(struct brw_vs_prog_key),
      [MESA_SHADER_TESS_CTRL] = sizeof(struct brw_tcs_prog_key),
      [MESA_SHADER_TESS_EVAL] = sizeof(struct brw_tes_prog_key),
      [MESA_SHADER_GEOMETRY]  = sizeof(struct brw_gs_prog_key),
      [MESA_SHADER_FRAGMENT]  = sizeof(struct brw_wm_prog_key),
      [MESA_SHADER_COMPUTE]   = sizeof(struct brw_cs_prog_key),
      [MESA_SHADER_KERNEL]    = sizeof(struct brw_cs_prog_key),
   };
   assert((int)stage >= 0 && stage < ARRAY_SIZE(stage_sizes));
   return stage_sizes[stage];
}

void
brw_write_shader_relocs(const struct gen_device_info *devinfo,
                        void *program,
                        const struct brw_stage_prog_data *prog_data,
                        struct brw_shader_reloc_value *values,
                        unsigned num_values)
{
   for (unsigned i = 0; i < prog_data->num_relocs; i++) {
      assert(prog_data->relocs[i].offset % 8 == 0);
      brw_inst *inst = (brw_inst *)(program + prog_data->relocs[i].offset);
      for (unsigned j = 0; j < num_values; j++) {
         if (prog_data->relocs[i].id == values[j].id) {
            brw_update_reloc_imm(devinfo, inst, values[j].value);
            break;
         }
      }
   }
}
