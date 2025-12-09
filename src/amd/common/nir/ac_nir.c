/*
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_gpu_info.h"
#include "ac_nir.h"
#include "ac_nir_helpers.h"
#include "nir_builder.h"
#include "nir_intrinsics.h"

/* Set NIR options shared by ACO, LLVM, RADV, and radeonsi. */
void ac_nir_set_options(struct radeon_info *info, bool use_llvm,
                        nir_shader_compiler_options *options)
{
   /*        |---------------------------------- Performance & Availability --------------------------------|
    *        |MAD/MAC/MADAK/MADMK|MAD_LEGACY|MAC_LEGACY|    FMA     |FMAC/FMAAK/FMAMK|FMA_LEGACY|PK_FMA_F16,|Best choice
    * Arch   |    F32,F16,F64    | F32,F16  | F32,F16  |F32,F16,F64 |    F32,F16     |   F32    |PK_FMAC_F16|F16,F32,F64
    * ------------------------------------------------------------------------------------------------------------------
    * gfx6,7 |     1 , - , -     |  1 , -   |  1 , -   |1/4, - ,1/16|     - , -      |    -     |   - , -   | - ,MAD,FMA
    * gfx8   |     1 , 1 , -     |  1 , -   |  - , -   |1/4, 1 ,1/16|     - , -      |    -     |   - , -   |MAD,MAD,FMA
    * gfx9   |     1 ,1|0, -     |  1 , -   |  - , -   | 1 , 1 ,1/16|    0|1, -      |    -     |   2 , -   |FMA,MAD,FMA
    * gfx10  |     1 , - , -     |  1 , -   |  1 , -   | 1 , 1 ,1/16|     1 , 1      |    -     |   2 , 2   |FMA,MAD,FMA
    * gfx10.3|     - , - , -     |  - , -   |  - , -   | 1 , 1 ,1/16|     1 , 1      |    1     |   2 , 2   |  all FMA
    * gfx11  |     - , - , -     |  - , -   |  - , -   | 2 , 2 ,1/16|     2 , 2      |    2     |   2 , 2   |  all FMA
    *
    * Tahiti, Hawaii, Carrizo, Vega20: FMA_F32 is full rate, FMA_F64 is 1/4
    * gfx9 supports MAD_F16 only on Vega10, Raven, Raven2, Renoir.
    * gfx9 supports FMAC_F32 only on Vega20, but doesn't support FMAAK and FMAMK.
    *
    * gfx8 prefers MAD for F16 because of MAC/MADAK/MADMK.
    * gfx9 and newer prefer FMA for F16 because of the packed instruction.
    * gfx10 and older prefer MAD for F32 because of the legacy instruction.
    */

   memset(options, 0, sizeof(*options));
   options->vertex_id_zero_based = true;
   options->lower_scmp = true;
   options->lower_flrp16 = true;
   options->lower_flrp32 = true;
   options->lower_flrp64 = true;
   options->lower_device_index_to_zero = true;
   options->lower_fdiv = true;
   options->lower_fmod = true;
   options->lower_ineg = true;
   options->lower_bitfield_insert = true;
   options->lower_bitfield_extract = true;
   options->lower_bitfield_extract16 = use_llvm;
   options->lower_bitfield_extract8 = use_llvm;
   options->lower_pack_snorm_4x8 = true;
   options->lower_pack_unorm_4x8 = true;
   options->lower_pack_half_2x16 = true;
   options->lower_pack_64_2x32 = true;
   options->lower_pack_64_4x16 = true;
   options->lower_pack_32_2x16 = true;
   options->lower_unpack_snorm_2x16 = true;
   options->lower_unpack_snorm_4x8 = true;
   options->lower_unpack_unorm_2x16 = true;
   options->lower_unpack_unorm_4x8 = true;
   options->lower_unpack_half_2x16 = true;
   options->lower_fpow = true;
   options->lower_mul_high16 = true;
   options->lower_mul_2x32_64 = true;
   options->lower_iadd_sat = info->gfx_level <= GFX8;
   options->lower_hadd = true;
   options->lower_mul_32x16 = true;
   options->lower_bfloat16_conversions = true,
   options->has_bfe = true;
   options->has_bfm = true;
   options->has_bitfield_select = true;
   options->has_fneo_fcmpu = true;
   options->has_ford_funord = true;
   options->has_fsub = true;
   options->has_isub = true;
   options->has_sdot_4x8 = info->has_accelerated_dot_product;
   options->has_sudot_4x8 = info->has_accelerated_dot_product && info->gfx_level >= GFX11;
   options->has_udot_4x8 = info->has_accelerated_dot_product;
   options->has_sdot_4x8_sat = info->has_accelerated_dot_product;
   options->has_sudot_4x8_sat = info->has_accelerated_dot_product && info->gfx_level >= GFX11;
   options->has_udot_4x8_sat = info->has_accelerated_dot_product;
   options->has_dot_2x16 = info->has_accelerated_dot_product && info->gfx_level < GFX11;
   options->has_bfdot2_bfadd = info->gfx_level >= GFX12;
   options->has_find_msb_rev = true;
   options->has_pack_32_4x8 = true;
   options->has_pack_half_2x16_rtz = true;
   options->has_bit_test = !use_llvm;
   options->has_fmulz = true;
   options->has_msad = true;
   options->has_shfr32 = true;
   options->has_mul24_relaxed = true;
   options->has_f2e4m3fn_satfn = !use_llvm && info->gfx_level >= GFX12;
   options->has_atomic_isub = true;
   options->has_atomic_load_store = true;
   options->lower_int64_options = nir_lower_imul64 | nir_lower_imul_high64 | nir_lower_imul_2x32_64 | nir_lower_divmod64 |
                                  nir_lower_minmax64 | nir_lower_iabs64 | nir_lower_iadd_sat64 | nir_lower_conv64 |
                                  nir_lower_bitfield_extract64;
   options->divergence_analysis_options = nir_divergence_view_index_uniform;
   options->support_indirect_inputs = BITFIELD_BIT(MESA_SHADER_TESS_CTRL) |
                                      BITFIELD_BIT(MESA_SHADER_TESS_EVAL);
   options->support_indirect_outputs = BITFIELD_BIT(MESA_SHADER_TESS_CTRL) |
                                       BITFIELD_BIT(MESA_SHADER_MESH);
   options->optimize_quad_vote_to_reduce = !use_llvm;
   options->lower_fisnormal = true;
   options->support_16bit_alu = info->gfx_level >= GFX8;
   options->vectorize_vec2_16bit = info->has_packed_math_16bit;
   options->discard_is_demote = true;
   options->optimize_sample_mask_in = true;
   options->optimize_load_front_face_fsign = true;
   options->io_options = nir_io_has_flexible_input_interpolation_except_flat |
                         (info->gfx_level >= GFX8 ? nir_io_16bit_input_output_support : 0) |
                         nir_io_prefer_scalar_fs_inputs |
                         nir_io_mix_convergent_flat_with_interpolated |
                         nir_io_vectorizer_ignores_types |
                         nir_io_compaction_rotates_color_channels |
                         nir_io_assign_color_input_bases_after_all_other_inputs;
   options->lower_layer_fs_input_to_sysval = true;
   options->scalarize_ddx = true;
   options->coarse_ddx = true;
   options->skip_lower_packing_ops =
      BITFIELD_BIT(nir_lower_packing_op_unpack_64_2x32) |
      BITFIELD_BIT(nir_lower_packing_op_unpack_64_4x16) |
      BITFIELD_BIT(nir_lower_packing_op_unpack_32_2x16) |
      BITFIELD_BIT(nir_lower_packing_op_pack_32_4x8) |
      BITFIELD_BIT(nir_lower_packing_op_unpack_32_4x8);
   options->max_workgroup_invocations = 1024;
   options->max_workgroup_count[0] = UINT32_MAX;
   options->max_workgroup_count[1] = UINT16_MAX;
   options->max_workgroup_count[2] = UINT16_MAX;
}

/* Sleep for the given number of clock cycles. */
void
ac_nir_sleep(nir_builder *b, unsigned num_cycles)
{
   /* s_sleep can only sleep for N*64 cycles. */
   if (num_cycles >= 64) {
      nir_sleep_amd(b, num_cycles / 64);
      num_cycles &= 63;
   }

   /* Use s_nop to sleep for the remaining cycles. */
   while (num_cycles) {
      unsigned nop_cycles = MIN2(num_cycles, 16);

      nir_nop_amd(b, nop_cycles - 1);
      num_cycles -= nop_cycles;
   }
}

/* Load argument with index start from arg plus relative_index. */
nir_def *
ac_nir_load_arg_at_offset(nir_builder *b, const struct ac_shader_args *ac_args,
                          struct ac_arg arg, unsigned relative_index)
{
   unsigned arg_index = arg.arg_index + relative_index;
   unsigned num_components = ac_args->args[arg_index].size;

   if (ac_args->args[arg_index].skip)
      return nir_undef(b, num_components, 32);

   if (ac_args->args[arg_index].file == AC_ARG_SGPR)
      return nir_load_scalar_arg_amd(b, num_components, .base = arg_index);
   else
      return nir_load_vector_arg_amd(b, num_components, .base = arg_index);
}

nir_def *
ac_nir_load_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg)
{
   return ac_nir_load_arg_at_offset(b, ac_args, arg, 0);
}

nir_def *
ac_nir_load_arg_upper_bound(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                            unsigned upper_bound)
{
   nir_def *value = ac_nir_load_arg_at_offset(b, ac_args, arg, 0);
   nir_intrinsic_set_arg_upper_bound_u32_amd(nir_def_as_intrinsic(value),
                                             upper_bound);
   return value;
}

void
ac_nir_store_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                 nir_def *val)
{
   assert(nir_cursor_current_block(b->cursor)->cf_node.parent->type == nir_cf_node_function);

   if (ac_args->args[arg.arg_index].file == AC_ARG_SGPR)
      nir_store_scalar_arg_amd(b, val, .base = arg.arg_index);
   else
      nir_store_vector_arg_amd(b, val, .base = arg.arg_index);
}

nir_def *
ac_nir_unpack_value(nir_builder *b, nir_def *value, unsigned rshift, unsigned bitwidth)
{
   if (rshift == 0 && bitwidth == 32)
      return value;
   else if (rshift == 0)
      return nir_iand_imm(b, value, BITFIELD_MASK(bitwidth));
   else if ((32 - rshift) <= bitwidth)
      return nir_ushr_imm(b, value, rshift);
   else
      return nir_ubfe_imm(b, value, rshift, bitwidth);
}

nir_def *
ac_nir_unpack_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                  unsigned rshift, unsigned bitwidth)
{
   nir_def *value = ac_nir_load_arg(b, ac_args, arg);
   return ac_nir_unpack_value(b, value, rshift, bitwidth);
}

bool
ac_nir_lower_indirect_derefs(nir_shader *shader,
                             enum amd_gfx_level gfx_level)
{
   bool progress = false;

   /* TODO: Don't lower convergent VGPR indexing because the hw can do it. */

   /* Lower large variables to scratch first so that we won't bloat the
    * shader by generating large if ladders for them.
    */
   NIR_PASS(progress, shader, nir_lower_vars_to_scratch, nir_var_function_temp, 256,
            glsl_get_natural_size_align_bytes, glsl_get_natural_size_align_bytes);

   /* This lowers indirect indexing to if-else ladders. */
   NIR_PASS(progress, shader, nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_function_temp, UINT32_MAX);
   return progress;
}

/* Shader logging function for printing nir_def values. The driver prints this after
 * command submission.
 *
 * Ring buffer layout: {uint32_t num_dwords; vec4; vec4; vec4; ... }
 * - The buffer size must be 2^N * 16 + 4
 * - num_dwords is incremented atomically and the ring wraps around, removing
 *   the oldest entries.
 */
void
ac_nir_store_debug_log_amd(nir_builder *b, nir_def *uvec4)
{
   nir_def *buf = nir_load_debug_log_desc_amd(b);
   nir_def *zero = nir_imm_int(b, 0);

   nir_def *max_index =
      nir_iadd_imm(b, nir_ushr_imm(b, nir_iadd_imm(b, nir_channel(b, buf, 2), -4), 4), -1);
   nir_def *index = nir_ssbo_atomic(b, 32, buf, zero, nir_imm_int(b, 1),
                                    .atomic_op = nir_atomic_op_iadd);
   index = nir_iand(b, index, max_index);
   nir_def *offset = nir_iadd_imm(b, nir_imul_imm(b, index, 16), 4);
   nir_store_buffer_amd(b, uvec4, buf, offset, zero, zero);
}

nir_def *
ac_average_samples(nir_builder *b, nir_def **samples, unsigned num_samples)
{
   /* This works like add-reduce by computing the sum of each pair independently, and then
    * computing the sum of each pair of sums, and so on, to get better instruction-level
    * parallelism.
    */
   if (num_samples == 16) {
      for (unsigned i = 0; i < 8; i++)
         samples[i] = nir_fadd(b, samples[i * 2], samples[i * 2 + 1]);
   }
   if (num_samples >= 8) {
      for (unsigned i = 0; i < 4; i++)
         samples[i] = nir_fadd(b, samples[i * 2], samples[i * 2 + 1]);
   }
   if (num_samples >= 4) {
      for (unsigned i = 0; i < 2; i++)
         samples[i] = nir_fadd(b, samples[i * 2], samples[i * 2 + 1]);
   }
   if (num_samples >= 2)
      samples[0] = nir_fadd(b, samples[0], samples[1]);

   return nir_fmul_imm(b, samples[0], 1.0 / num_samples); /* average the sum */
}

void
ac_optimization_barrier_vgpr_array(const struct radeon_info *info, nir_builder *b,
                                   nir_def **array, unsigned num_elements,
                                   unsigned num_components)
{
   /* We use the optimization barrier to force LLVM to form VMEM clauses by constraining its
    * instruction scheduling options.
    *
    * VMEM clauses are supported since GFX10. It's not recommended to use the optimization
    * barrier in the compute blit for GFX6-8 because the lack of A16 combined with optimization
    * barriers would unnecessarily increase VGPR usage for MSAA resources.
    */
   if (!b->shader->info.use_aco_amd && info->gfx_level >= GFX10) {
      for (unsigned i = 0; i < num_elements; i++) {
         unsigned prev_num = array[i]->num_components;
         array[i] = nir_trim_vector(b, array[i], num_components);
         array[i] = nir_optimization_barrier_vgpr_amd(b, array[i]->bit_size, array[i]);
         array[i] = nir_pad_vector(b, array[i], prev_num);
      }
   }
}

nir_def *
ac_get_global_ids(nir_builder *b, unsigned num_components, unsigned bit_size)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_def *block_ids = nir_channels(b, nir_load_workgroup_id(b), mask);
   nir_def *block_size = nir_channels(b, nir_load_workgroup_size(b), mask);

   assert(bit_size == 32 || bit_size == 16);
   if (bit_size == 16) {
      local_ids = nir_i2iN(b, local_ids, bit_size);
      block_ids = nir_i2iN(b, block_ids, bit_size);
      block_size = nir_i2iN(b, block_size, bit_size);
   }

   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

nir_def *
ac_nir_load_smem(nir_builder *b, unsigned num_components, nir_def *addr, nir_def *offset,
                 unsigned align_mul, enum gl_access_qualifier access)
{
   /* Only 1 flag is allowed. */
   assert(!(access & ~ACCESS_CAN_SPECULATE));
   assert(align_mul >= 4 && util_is_power_of_two_nonzero(align_mul));
   if (addr->bit_size == 32)
      addr = nir_iadd_nuw(b, addr, offset);
   else
      addr = nir_iadd(b, addr, nir_u2u64(b, offset));

   return nir_load_global(b, num_components, 32, addr,
                            .align_mul = align_mul,
                            .access = access | ACCESS_CAN_REORDER | ACCESS_NON_WRITEABLE | ACCESS_SMEM_AMD);
}

unsigned
ac_nir_varying_expression_max_cost(nir_shader *producer, nir_shader *consumer)
{
   switch (consumer->info.stage) {
   case MESA_SHADER_TESS_CTRL:
      /* VS->TCS
       * Non-amplifying shaders can always have their varying expressions
       * moved into later shaders.
       */
      return UINT_MAX;

   case MESA_SHADER_GEOMETRY:
      /* VS->GS, TES->GS */
      return consumer->info.gs.vertices_in == 1 ? UINT_MAX :
             consumer->info.gs.vertices_in == 2 ? 20 : 14;

   case MESA_SHADER_TESS_EVAL:
      /* TCS->TES and VS->TES (OpenGL only) */
   case MESA_SHADER_FRAGMENT:
      /* Up to 3 uniforms and 5 ALUs. */
      return 12;

   default:
      UNREACHABLE("unexpected shader stage");
   }
}

bool
ac_nir_optimize_uniform_atomics(nir_shader *nir)
{
   bool progress = false;
   NIR_PASS(progress, nir, ac_nir_opt_shared_append);
   NIR_PASS(progress, nir, nir_opt_uniform_atomics, false);

   return progress;
}

static unsigned
lower_bit_size_callback(const nir_instr *instr, enum amd_gfx_level chip, bool divergence_known)
{
   if (instr->type != nir_instr_type_alu)
      return 0;
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   /* If an instruction is not scalarized by this point,
    * it can be emitted as packed instruction */
   if (alu->def.num_components > 1)
      return 0;

   if (alu->def.bit_size & (8 | 16)) {
      unsigned bit_size = alu->def.bit_size;
      switch (alu->op) {
      case nir_op_imul_high:
      case nir_op_umul_high:
      case nir_op_uadd_carry:
      case nir_op_usub_borrow:
         return 32;
      case nir_op_iabs:
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_imin:
      case nir_op_umin:
      case nir_op_ishr:
      case nir_op_ushr:
      case nir_op_ishl:
      case nir_op_isign:
      case nir_op_uadd_sat:
      case nir_op_usub_sat:
         return (!divergence_known || bit_size == 8 || !(chip >= GFX8 && alu->def.divergent)) ? 32 : 0;
      case nir_op_iadd_sat:
      case nir_op_isub_sat:
         return !divergence_known || bit_size == 8 || !alu->def.divergent ? 32 : 0;
      case nir_op_extract_u8:
      case nir_op_extract_i8:
         return !divergence_known || !alu->def.divergent ? 32 : 0;

      default:
         return 0;
      }
   }

   if (nir_src_bit_size(alu->src[0].src) & (8 | 16)) {
      unsigned bit_size = nir_src_bit_size(alu->src[0].src);
      switch (alu->op) {
      case nir_op_bit_count:
      case nir_op_find_lsb:
      case nir_op_ufind_msb:
         return 32;
      case nir_op_ilt:
      case nir_op_ige:
      case nir_op_ieq:
      case nir_op_ine:
      case nir_op_ult:
      case nir_op_uge:
      case nir_op_bitz:
      case nir_op_bitnz:
         return (!divergence_known || bit_size == 8 || !(chip >= GFX8 && alu->def.divergent)) ? 32 : 0;
      default:
         return 0;
      }
   }

   return 0;
}

unsigned
ac_nir_lower_bit_size_callback(const nir_instr *instr, void *data)
{
   enum amd_gfx_level chip = *(enum amd_gfx_level *)data;
   return lower_bit_size_callback(instr, chip, true);
}

bool
ac_nir_might_lower_bit_size(const nir_shader *shader)
{
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (lower_bit_size_callback(instr, CLASS_UNKNOWN, false))
               return true;
         }
      }
   }

   return false;
}

static unsigned
align_load_store_size(enum amd_gfx_level gfx_level, unsigned size, bool uses_smem, bool is_shared)
{
   /* LDS can't overfetch because accesses that are partially out of range would be dropped
    * entirely, so all unaligned LDS accesses are always split.
    */
   if (is_shared)
      return size;

   /* Align the size to what the hw supports. Out of range access due to alignment is OK because
    * range checking is per dword for untyped instructions. This assumes that the compiler backend
    * overfetches due to load size alignment instead of splitting the load.
    *
    * GFX6-11 don't have 96-bit SMEM loads.
    * GFX6 doesn't have 96-bit untyped VMEM loads.
    */
   if (gfx_level >= (uses_smem ? GFX12 : GFX7) && size == 96)
      return size;
   else
      return util_next_power_of_two(size);
}

bool
ac_nir_mem_vectorize_callback(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                              unsigned num_components, int64_t hole_size, nir_intrinsic_instr *low,
                              nir_intrinsic_instr *high, void *data)
{
   struct ac_nir_config *config = (struct ac_nir_config *)data;
   bool uses_smem = (nir_intrinsic_has_access(low) &&
                     nir_intrinsic_access(low) & ACCESS_SMEM_AMD) ||
                    /* These don't have the "access" field. */
                    low->intrinsic == nir_intrinsic_load_push_constant;
   bool is_store = !nir_intrinsic_infos[low->intrinsic].has_dest;
   bool swizzled = low->intrinsic == nir_intrinsic_load_stack ||
                    low->intrinsic == nir_intrinsic_store_stack ||
                    low->intrinsic == nir_intrinsic_load_scratch ||
                    low->intrinsic == nir_intrinsic_store_scratch ||
                    (nir_intrinsic_has_access(low) &&
                     nir_intrinsic_access(low) & ACCESS_IS_SWIZZLED_AMD);
   bool is_shared = low->intrinsic == nir_intrinsic_load_shared ||
                    low->intrinsic == nir_intrinsic_store_shared ||
                    low->intrinsic == nir_intrinsic_load_deref ||
                    low->intrinsic == nir_intrinsic_store_deref;
   unsigned swizzle_element_size = config->gfx_level <= GFX8 ? 4 : 16;

   assert(!is_store || hole_size <= 0);

   /* If we get derefs here, only shared memory derefs are expected. */
   assert((low->intrinsic != nir_intrinsic_load_deref &&
           low->intrinsic != nir_intrinsic_store_deref) ||
          nir_deref_mode_is(nir_src_as_deref(low->src[0]), nir_var_mem_shared));

   /* Don't vectorize descriptor loads for LLVM due to excessive SGPR and VGPR spilling. */
   if (!config->uses_aco && low->intrinsic == nir_intrinsic_load_global && uses_smem)
      return false;

   /* Reject opcodes we don't vectorize. */
   switch (low->intrinsic) {
   case nir_intrinsic_load_push_constant:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_stack:
   case nir_intrinsic_store_stack:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_store_scratch:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_global:
   case nir_intrinsic_store_global:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_load_buffer_amd:
   case nir_intrinsic_store_buffer_amd:
      break;
   default:
      return false;
   }

   /* Align the size to what the hw supports. */
   unsigned unaligned_new_size = num_components * bit_size;
   unsigned aligned_new_size = align_load_store_size(config->gfx_level, unaligned_new_size,
                                                     uses_smem, is_shared);

   if (uses_smem) {
      /* Maximize SMEM vectorization except for LLVM, which suffers from SGPR and VGPR spilling.
       * GFX6-7 have fewer hw SGPRs, so merge only up to 128 bits to limit SGPR usage.
       */
      if (aligned_new_size > (config->gfx_level >= GFX8 ? (config->uses_aco ? 512 : 256) : 128))
         return false;
   } else {
      if (aligned_new_size > 128)
         return false;

      /* GFX6-8 only support 32-bit scratch loads/stores. */
      if (swizzled && aligned_new_size > (swizzle_element_size * 8))
         return false;
   }

   if (!is_store) {
      /* Non-descriptor loads. */
      if (low->intrinsic != nir_intrinsic_load_ubo &&
          low->intrinsic != nir_intrinsic_load_ssbo) {
         /* Only increase the size of loads if doing so doesn't extend into a new page.
          * Here we set alignment to MAX because we don't know the alignment of global
          * pointers before adding the offset.
          */
         uint32_t resource_align = low->intrinsic == nir_intrinsic_load_global_constant ||
                                   low->intrinsic == nir_intrinsic_load_global ? NIR_ALIGN_MUL_MAX : 4;
         uint32_t page_size = 4096;
         uint32_t mul = MIN3(align_mul, page_size, resource_align);
         unsigned end = (align_offset + unaligned_new_size / 8u) & (mul - 1);
         if ((aligned_new_size - unaligned_new_size) / 8u > (mul - end))
            return false;
      }

      /* Only allow SMEM loads to overfetch by 32 bits:
       *
       * Examples (the hole is indicated by parentheses, the numbers are  in bytes, the maximum
       * overfetch size is 4):
       *    4  | (4) | 4   ->  hw loads 12  : ALLOWED    (4 over)
       *    4  | (4) | 4   ->  hw loads 16  : DISALLOWED (8 over)
       *    4  |  4  | 4   ->  hw loads 16  : ALLOWED    (4 over)
       *    4  | (4) | 8   ->  hw loads 16  : ALLOWED    (4 over)
       *    16 |  4        ->  hw loads 32  : DISALLOWED (12 over)
       *    16 |  8        ->  hw loads 32  : DISALLOWED (8 over)
       *    16 | 12        ->  hw loads 32  : ALLOWED    (4 over)
       *    16 | (4) | 12  ->  hw loads 32  : ALLOWED    (4 over)
       *    32 | 16        ->  hw loads 64  : DISALLOWED (16 over)
       *    32 | 28        ->  hw loads 64  : ALLOWED    (4 over)
       *    32 | (4) | 28  ->  hw loads 64  : ALLOWED    (4 over)
       *
       * Note that we can overfetch by more than 4 bytes if we merge more than 2 loads, e.g.:
       *    4  | (4) | 8 | (4) | 12  ->  hw loads 32  : ALLOWED (4 + 4 over)
       *
       * That's because this callback is called twice in that case, each time allowing only 4 over.
       *
       * This is only enabled for ACO. LLVM spills SGPRs and VGPRs too much.
       */
      unsigned overfetch_size = 0;

      if (config->uses_aco && uses_smem && aligned_new_size >= 128)
         overfetch_size = 32;

      /* Allow overfetching from 8/16 bits to 32 bits. */
      int64_t aligned_unvectorized_size =
         ALIGN_POT(align_load_store_size(config->gfx_level, low->num_components * low->def.bit_size,
                                         uses_smem, is_shared), 32) +
         ALIGN_POT(align_load_store_size(config->gfx_level, high->num_components * high->def.bit_size,
                                         uses_smem, is_shared), 32);

      if (ALIGN_POT(aligned_new_size, 32) > aligned_unvectorized_size + overfetch_size)
         return false;
   }

   uint32_t align = nir_combined_align(align_mul, align_offset);

   /* Don't cross swizzle elements. stack/scratch intrinsics use scratch_* instructions, which
    * seem to work fine.
    */
   if ((low->intrinsic == nir_intrinsic_load_buffer_amd ||
        low->intrinsic == nir_intrinsic_store_buffer_amd) && swizzled &&
       (align_offset % swizzle_element_size + unaligned_new_size / 8u) > MIN2(align_mul, swizzle_element_size)) {
      return false;
   }

   /* Validate the alignment and number of components. */
   if (!is_shared) {
      return (align % (bit_size / 8u)) == 0 && num_components <= NIR_MAX_VEC_COMPONENTS;
   } else {
      /* 96-bit and 128-bit LDS loads are slow. Don't use them. */
      if (!is_store && bit_size * num_components > 64)
         return false;
      if (bit_size >= 32 && num_components == 3) {
         /* AMD hardware can't do 3-component loads except for 96-bit loads. */
         return bit_size == 32 && align % 16 == 0;
      }

      /* DS loads and stores require the alignment of the size. */
      return align % (aligned_new_size / 8u) == 0;
   }
   return false;
}

bool ac_nir_scalarize_overfetching_loads_callback(const nir_intrinsic_instr *intr, const void *data)
{
   /* Reject opcodes we don't scalarize. */
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_shared:
      break;
   default:
      return false;
   }

   bool uses_smem = nir_intrinsic_has_access(intr) &&
                    nir_intrinsic_access(intr) & ACCESS_SMEM_AMD;
   bool is_shared = intr->intrinsic == nir_intrinsic_load_shared;

   enum amd_gfx_level gfx_level = *(enum amd_gfx_level *)data;
   unsigned comp_size = intr->def.bit_size / 8;
   unsigned load_size = intr->def.num_components * comp_size;
   unsigned used_load_size = util_bitcount(nir_def_components_read(&intr->def)) * comp_size;

   /* Scalarize if the load overfetches. That includes loads that overfetch due to load size
    * alignment, e.g. when only a power-of-two load is available. The scalarized loads are expected
    * to be later vectorized to optimal sizes.
    */
   return used_load_size < align_load_store_size(gfx_level, load_size, uses_smem, is_shared);
}

/* Determine if the store can be subdword (for the GFX6 TC L1 bug workaround) */
bool ac_nir_store_may_be_subdword(const nir_intrinsic_instr *instr)
{
   assert(!nir_intrinsic_infos[instr->intrinsic].has_dest);
   switch (instr->intrinsic) {
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_buffer_amd:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_amd:
      return (nir_intrinsic_has_align_offset(instr) && nir_intrinsic_align(instr) % 4 != 0) ||
             ((instr->src[0].ssa->bit_size / 8) * instr->src[0].ssa->num_components) % 4 != 0;


   default:
      UNREACHABLE("unexpected store instruction");
   }

   return false;
}

/**
 * Computes a horizontal sum of 8-bit packed values loaded from LDS.
 *
 * Each lane N will sum packed bytes 0 to N.
 * We only care about the results from up to wave_id lanes.
 * (Other lanes are not deactivated but their calculation is not used.)
 */
static nir_def *
summarize_repack(nir_builder *b, nir_def *packed_counts, bool mask_lane_id, unsigned num_lds_dwords)
{
   /* We'll use shift to filter out the bytes not needed by the current lane.
    *
    * For each row:
    * Need to shift by: `num_lds_dwords * 4 - 1 - lane_id_in_row` (in bytes)
    * in order to implement an inclusive scan.
    *
    * When v_dot4_u32_u8 is available, we right-shift a series of 0x01 bytes.
    * This will yield 0x01 at wanted byte positions and 0x00 at unwanted positions,
    * therefore v_dot can get rid of the unneeded values.
    *
    * If the v_dot instruction can't be used, we left-shift the packed bytes
    * in order to shift out the unneeded bytes and shift in zeroes instead,
    * then we sum them using v_msad_u8.
    */

   nir_def *lane_id = nir_load_subgroup_invocation(b);

   /* Mask lane ID so that lanes 16...31 also have the ID 0...15,
    * in order to perform a second horizontal sum in parallel when needed.
    */
   if (mask_lane_id)
      lane_id = nir_iand_imm(b, lane_id, 0xf);

   nir_def *shift = nir_iadd_imm(b, nir_imul_imm(b, lane_id, -8u), num_lds_dwords * 32 - 8);
   assert(b->shader->options->has_msad || b->shader->options->has_udot_4x8);
   bool use_dot = b->shader->options->has_udot_4x8;

   if (num_lds_dwords == 1) {
      /* Broadcast the packed data we read from LDS
       * (to the first 16 lanes of the row, but we only care up to num_waves).
       */
      nir_def *packed = nir_lane_permute_16_amd(b, packed_counts, nir_imm_int(b, 0), nir_imm_int(b, 0));

      /* Horizontally add the packed bytes. */
      if (use_dot) {
         nir_def *dot_op = nir_ushr(b, nir_imm_int(b, 0x01010101), shift);
         return nir_udot_4x8_uadd(b, packed, dot_op, nir_imm_int(b, 0));
      } else {
         nir_def *sad_op = nir_ishl(b, packed, shift);
         return nir_msad_4x8(b, sad_op, nir_imm_int(b, 0), nir_imm_int(b, 0));
      }
   } else if (num_lds_dwords == 2) {
      /* Broadcast the packed data we read from LDS
       * (to the first 16 lanes of the row, but we only care up to num_waves).
       */
      nir_def *packed_dw0 = nir_lane_permute_16_amd(b, nir_unpack_64_2x32_split_x(b, packed_counts), nir_imm_int(b, 0), nir_imm_int(b, 0));
      nir_def *packed_dw1 = nir_lane_permute_16_amd(b, nir_unpack_64_2x32_split_y(b, packed_counts), nir_imm_int(b, 0), nir_imm_int(b, 0));

      /* Horizontally add the packed bytes. */
      if (use_dot) {
         nir_def *dot_op = nir_ushr(b, nir_imm_int64(b, 0x0101010101010101), shift);
         nir_def *sum = nir_udot_4x8_uadd(b, packed_dw0, nir_unpack_64_2x32_split_x(b, dot_op), nir_imm_int(b, 0));
         return nir_udot_4x8_uadd(b, packed_dw1, nir_unpack_64_2x32_split_y(b, dot_op), sum);
      } else {
         nir_def *sad_op = nir_ishl(b, nir_pack_64_2x32_split(b, packed_dw0, packed_dw1), shift);
         nir_def *sum = nir_msad_4x8(b, nir_unpack_64_2x32_split_x(b, sad_op), nir_imm_int(b, 0), nir_imm_int(b, 0));
         return nir_msad_4x8(b, nir_unpack_64_2x32_split_y(b, sad_op), nir_imm_int(b, 0), sum);
      }
   } else {
      UNREACHABLE("Unimplemented NGG wave count");
   }
}

/**
 * Repacks invocations in the current workgroup to eliminate gaps between them.
 *
 * Uses 1 dword of LDS per 4 waves (1 byte of LDS per wave) for each repack.
 * Assumes that all invocations in the workgroup are active (exec = -1).
 */
void
ac_nir_repack_invocations_in_workgroup(nir_builder *b, nir_def **input_bool,
                                       ac_nir_wg_repack_result *results, const unsigned num_repacks,
                                       nir_def *lds_addr_base, unsigned max_num_waves,
                                       unsigned wave_size)
{
   /* We can currently only do up to 2 repacks at a time. */
   assert(num_repacks <= 2);

   /* STEP 1. Count surviving invocations in the current wave.
    *
    * Implemented by a scalar instruction that simply counts the number of bits set in a 32/64-bit mask.
    */

   nir_def *input_mask[2];
   nir_def *surviving_invocations_in_current_wave[2];

   for (unsigned i = 0; i < num_repacks; ++i) {
      /* Input should be boolean: 1 if the current invocation should survive the repack. */
      assert(input_bool[i]->bit_size == 1);

      input_mask[i] = nir_ballot(b, 1, wave_size, input_bool[i]);
      surviving_invocations_in_current_wave[i] = nir_bit_count(b, input_mask[i]);
   }

   /* If we know at compile time that the workgroup has only 1 wave, no further steps are necessary. */
   if (max_num_waves == 1) {
      for (unsigned i = 0; i < num_repacks; ++i) {
         results[i].num_repacked_invocations = surviving_invocations_in_current_wave[i];
         results[i].repacked_invocation_index = nir_mbcnt_amd(b, input_mask[i], nir_imm_int(b, 0));
      }
      return;
   }

   /* STEP 2. Waves tell each other their number of surviving invocations.
    *
    * Row 0 (lanes 0-15) performs the first repack, and Row 1 (lanes 16-31) the second in parallel.
    * Each wave activates only its first lane per row, which stores the number of surviving
    * invocations in that wave into the LDS for that repack, then reads the numbers from every wave.
    *
    * The workgroup size of NGG shaders is at most 256, which means
    * the maximum number of waves is 4 in Wave64 mode and 8 in Wave32 mode.
    * For each repack:
    * Each wave writes 1 byte, so it's up to 8 bytes, so at most 2 dwords are necessary.
    * (The maximum is 4 dwords for 2 repacks in Wave32 mode.)
    */

   const unsigned num_lds_dwords = DIV_ROUND_UP(max_num_waves, 4);
   assert(num_lds_dwords <= 2);

   /* The first lane of each row (per repack) needs to access the LDS. */
   const unsigned ballot = num_repacks == 1 ? 1 : 0x10001;

   nir_def *wave_id = nir_load_subgroup_id(b);
   nir_def *dont_care = nir_undef(b, 1, num_lds_dwords * 32);
   nir_def *packed_counts = NULL;

   nir_if *if_use_lds = nir_push_if(b, nir_inverse_ballot_imm(b, ballot, wave_size));
   {
      nir_def *store_val = surviving_invocations_in_current_wave[0];

      if (num_repacks == 2) {
         nir_def *lane_id_0 = nir_inverse_ballot_imm(b, 1, wave_size);
         nir_def *off = nir_bcsel(b, lane_id_0, nir_imm_int(b, 0), nir_imm_int(b, num_lds_dwords * 4));
         lds_addr_base = nir_iadd_nuw(b, lds_addr_base, off);
         store_val = nir_bcsel(b, lane_id_0, store_val, surviving_invocations_in_current_wave[1]);
      }

      nir_def *store_byte = nir_u2u8(b, store_val);
      nir_def *lds_offset = nir_iadd(b, lds_addr_base, wave_id);
      nir_store_shared(b, store_byte, lds_offset);

      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
                     .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);

      packed_counts = nir_load_shared(b, 1, num_lds_dwords * 32, lds_addr_base, .align_mul = 8u);
   }
   nir_pop_if(b, if_use_lds);

   packed_counts = nir_if_phi(b, packed_counts, dont_care);

   /* STEP 3. Compute the repacked invocation index and the total number of surviving invocations.
    *
    * By now, every wave knows the number of surviving invocations in all waves.
    * Each number is 1 byte, and they are packed into up to 2 dwords.
    *
    * For each row (of 16 lanes):
    * Each lane N (in the row) will sum the number of surviving invocations inclusively from waves 0 to N.
    * If the workgroup has M waves, then each row will use only its first M lanes for this.
    * (Other lanes are not deactivated but their calculation is not used.)
    *
    * - We read the sum from the lane whose id  (in the row) is the current wave's id,
    *   and subtract the number of its own surviving invocations.
    *   Add the masked bitcount to this, and we get the repacked invocation index.
    * - We read the sum from the lane whose id (in the row) is the number of waves in the workgroup minus 1.
    *   This is the total number of surviving invocations in the workgroup.
    */

   nir_def *num_waves = nir_load_num_subgroups(b);
   nir_def *sum = summarize_repack(b, packed_counts, num_repacks == 2, num_lds_dwords);

   for (unsigned i = 0; i < num_repacks; ++i) {
      nir_def *index_base_lane = nir_iadd_imm_nuw(b, wave_id, i * 16);
      nir_def *num_invocartions_lane = nir_iadd_imm(b, num_waves, i * 16 - 1);
      nir_def *wg_repacked_index_base =
         nir_isub(b, nir_read_invocation(b, sum, index_base_lane), surviving_invocations_in_current_wave[i]);
      results[i].num_repacked_invocations =
         nir_read_invocation(b, sum, num_invocartions_lane);
      results[i].repacked_invocation_index =
         nir_mbcnt_amd(b, input_mask[i], wg_repacked_index_base);
   }
}

uint8_t
ac_nir_lower_phis_to_scalar_cb(const nir_instr *instr, const void *_)
{
   nir_phi_instr *phi = nir_instr_as_phi(instr);
   if (phi->def.bit_size == 1 || phi->def.bit_size >= 32)
      return 1;

   return 32 / phi->def.bit_size;
}

bool
ac_nir_allow_offset_wrap_cb(nir_intrinsic_instr *instr, const void *data)
{
   enum amd_gfx_level gfx_level = *(enum amd_gfx_level *)data;
   switch (instr->intrinsic) {
   case nir_intrinsic_load_shared:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
   case nir_intrinsic_load_shared2_amd:
   case nir_intrinsic_store_shared2_amd:
      /* GFX6 uses a 16-bit adder and can't handle unsigned wrap. */
      return gfx_level >= GFX7;
   default: return false;
   }
}

/* This only applies to ACO, not LLVM, but it's not part of ACO because it's used by this shared
 * code that doesn't always link with ACO like tests.
 */
bool
ac_nir_op_supports_packed_math_16bit(const nir_alu_instr* alu)
{
   switch (alu->op) {
   case nir_op_f2f16: {
      nir_shader* shader = nir_cf_node_get_function(&alu->instr.block->cf_node)->function->shader;
      unsigned execution_mode = shader->info.float_controls_execution_mode;
      return (shader->options->force_f2f16_rtz && !nir_is_rounding_mode_rtne(execution_mode, 16)) ||
             nir_is_rounding_mode_rtz(execution_mode, 16);
   }
   case nir_op_fadd:
   case nir_op_fsub:
   case nir_op_fmul:
   case nir_op_ffma:
   case nir_op_fdiv:
   case nir_op_flrp:
   case nir_op_fabs:
   case nir_op_fneg:
   case nir_op_fsat:
   case nir_op_fmin:
   case nir_op_fmax:
   case nir_op_f2f16_rtz:
   case nir_op_iabs:
   case nir_op_iadd:
   case nir_op_iadd_sat:
   case nir_op_uadd_sat:
   case nir_op_isub:
   case nir_op_isub_sat:
   case nir_op_usub_sat:
   case nir_op_ineg:
   case nir_op_imul:
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
   case nir_op_extract_u8:
   case nir_op_extract_i8:
   case nir_op_ishl:
   case nir_op_ishr:
   case nir_op_ushr: return true;
   case nir_op_u2u16:
   case nir_op_i2i16: return alu->src[0].src.ssa->bit_size == 8;
   default: return false;
   }
}

static uint8_t
max_alu_src_identity_swizzle(const nir_alu_instr *alu, const nir_alu_src *src)
{
   uint8_t max_vector = 32 / alu->def.bit_size;
   if (nir_src_is_const(src->src))
      return max_vector;

   /* Return the number of correctly swizzled components. */
   for (unsigned i = 1; i < alu->def.num_components; i++) {
      if (src->swizzle[i] != src->swizzle[0] + i)
         /* Ensure that the result is a power of 2. */
         return MAX2(i & 0x6, 1);
   }

   return max_vector;
}

uint8_t
ac_nir_opt_vectorize_cb(const nir_instr *instr, const void *data)
{
   if (instr->type != nir_instr_type_alu)
      return 0;

   enum amd_gfx_level gfx_level = *(enum amd_gfx_level*)data;
   if (gfx_level < GFX9)
      return 1;

   const nir_alu_instr *alu = nir_instr_as_alu(instr);

   switch (alu->op) {
   case nir_op_f2e4m3fn:
   case nir_op_f2e4m3fn_sat:
   case nir_op_f2e4m3fn_satfn:
   case nir_op_f2e5m2:
   case nir_op_f2e5m2_sat:
   case nir_op_e4m3fn2f:
   case nir_op_e5m22f:
      return 2;
   default:
      break;
   }

   const unsigned bit_size = alu->def.bit_size;
   if (bit_size == 16 && ac_nir_op_supports_packed_math_16bit(alu))
      return 2;

   if (bit_size != 8 && bit_size != 16)
      return 1;

   /* Keep some opcodes vectorized if the operation can be performed as
    * 32-bit instruction with packed sources. The condition is that the
    * sources must have identity swizzles. */
   uint8_t target_width = 32 / bit_size;
   switch (alu->op) {
   case nir_op_bcsel:
      /* Must have scalar condition. */
      for (unsigned i = 1; i < alu->def.num_components; i++) {
         if (alu->src[0].swizzle[i] != alu->src[0].swizzle[0])
            return 1;
      }
      for (unsigned idx = 1; idx < 3; idx++)
         target_width = MIN2(target_width, max_alu_src_identity_swizzle(alu, &alu->src[idx]));
      break;
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
   case nir_op_inot:
   case nir_op_bitfield_select:
      for (unsigned idx = 0; idx < nir_op_infos[alu->op].num_inputs; idx++)
         target_width = MIN2(target_width, max_alu_src_identity_swizzle(alu, &alu->src[idx]));
      break;
   default:
      return 1;
   }

   return target_width;
}
