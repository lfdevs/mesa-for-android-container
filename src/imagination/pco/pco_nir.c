/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir.c
 *
 * \brief NIR-specific functions.
 */

#include "nir/nir_builder.h"
#include "nir/nir_lower_blend.h"
#include "pco.h"
#include "pco_internal.h"
#include "pvr_limits.h"

#include <stdio.h>

/** SPIR-V to NIR options. */
static const struct spirv_to_nir_options spirv_options = {
   .environment = NIR_SPIRV_VULKAN,

   .ubo_addr_format = nir_address_format_vec2_index_32bit_offset,
   .ssbo_addr_format = nir_address_format_vec2_index_32bit_offset,
   .phys_ssbo_addr_format = nir_address_format_2x32bit_global,
   .push_const_addr_format = nir_address_format_32bit_offset,
   .shared_addr_format = nir_address_format_32bit_offset,

   .min_ubo_alignment = PVR_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
   .min_ssbo_alignment = PVR_STORAGE_BUFFER_OFFSET_ALIGNMENT,
};

/** NIR options. */
static const nir_shader_compiler_options nir_options = {
   .discard_is_demote = true,
   .fuse_ffma32 = true,

   .has_f2i32_rtne = true,
   .has_fused_comp_and_csel = true,

   .instance_id_includes_base_index = true,

   .lower_device_index_to_zero = true,

   .lower_fdiv = true,
   .lower_ffract = true,
   .lower_find_lsb = true,
   .lower_fquantize2f16 = true,
   .lower_flrp32 = true,
   .lower_fmod = true,
   .lower_fpow = true,
   .lower_fsqrt = true,
   .lower_ftrunc = true,
   .lower_ifind_msb = true,
   .lower_ldexp = true,
   .lower_layer_fs_input_to_sysval = true,
   .lower_uadd_carry = true,
   .lower_uadd_sat = true,
   .lower_usub_borrow = true,
   .lower_usub_sat = true,
   .lower_mul_2x32_64 = true,
   .compact_arrays = true,
   .scalarize_ddx = true,

   .lower_int64_options = ~0U,
   .lower_pack_64_2x32 = true,
   .lower_pack_64_2x32_split = true,
   .lower_unpack_64_2x32_split = true,

   .lower_interpolate_at = true,

   .lower_helper_invocation = true,

   .max_unroll_iterations = 16,

   .io_options = nir_io_vectorizer_ignores_types,
};

/**
 * \brief Returns the SPIR-V to NIR options.
 *
 * \return The SPIR-V to NIR options.
 */
const struct spirv_to_nir_options *pco_spirv_options(void)
{
   return &spirv_options;
}

/**
 * \brief Returns the NIR options for a PCO compiler
 * context.
 *
 * \return The NIR options.
 */
const nir_shader_compiler_options *pco_nir_options(void)
{
   return &nir_options;
}

/**
 * \brief Returns the GLSL type size.
 *
 * \param[in] type Type.
 * \param[in] bindless Whether the access is bindless.
 * \return The size.
 */
static int glsl_type_size(const struct glsl_type *type, UNUSED bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/**
 * \brief Returns the vectorization with for a given instruction.
 *
 * \param[in] instr Instruction.
 * \param[in] data User data.
 * \return The vectorization width.
 */
static uint8_t vectorize_filter(const nir_instr *instr, UNUSED const void *data)
{
   if (instr->type == nir_instr_type_load_const)
      return 1;

   if (instr->type != nir_instr_type_alu)
      return 0;

   /* TODO */
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   default:
      break;
   }

   /* Basic for now. */
   return 2;
}

/**
 * \brief Filter for fragment shader inputs that need to be scalar.
 *
 * \param[in] instr Instruction.
 * \param[in] data User data.
 * \return True if the instruction was found.
 */
static bool frag_in_scalar_filter(const nir_intrinsic_instr *intr, const void *data)
{
   nir_shader *nir = (nir_shader *)data;

   if (intr->intrinsic != nir_intrinsic_load_input &&
       intr->intrinsic != nir_intrinsic_load_interpolated_input) {
      return false;
   }

   gl_varying_slot location = nir_intrinsic_io_semantics(intr).location;
   if (location == VARYING_SLOT_POS)
      return true;

   nir_variable *var =
      nir_find_variable_with_location(nir, nir_var_shader_in, location);
   assert(var);

   enum glsl_interp_mode interp_mode = var->data.interpolation;

   if (intr->intrinsic == nir_intrinsic_load_interpolated_input) {
      nir_intrinsic_instr *bary = nir_src_as_intrinsic(intr->src[0]);
      assert(bary);

      interp_mode = nir_intrinsic_interp_mode(bary);
   }

   if (interp_mode == INTERP_MODE_FLAT)
      return true;

   return false;
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size;
}

/**
 * \brief Checks whether two varying variables are the same.
 *
 * \param[in] out_var The first varying being compared.
 * \param[in] in_var The second varying being compared.
 * \return True if the varyings match.
 */
static bool varyings_match(nir_variable *out_var, nir_variable *in_var)
{
   return in_var->data.location == out_var->data.location &&
          in_var->data.location_frac == out_var->data.location_frac &&
          in_var->type == out_var->type;
}

/**
 * \brief Gather fragment shader data pass.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in,out] cb_data Callback data.
 * \return True if the shader was modified (always return false).
 */
static bool gather_fs_data_pass(struct nir_builder *b,
                                nir_intrinsic_instr *intr,
                                void *cb_data)
{
   pco_data *data = cb_data;

   switch (intr->intrinsic) {
   /* Check whether the shader accesses z/w. */
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input: {
      struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
      if (io_semantics.location != VARYING_SLOT_POS)
         return false;

      unsigned component = nir_intrinsic_component(intr);
      unsigned chans = intr->def.num_components;
      assert(component == 2 || chans == 1);

      data->fs.uses.z |= (component == 2);
      data->fs.uses.w |= (component + chans > 3);
      break;
   }

   case nir_intrinsic_load_fs_coeffs_pco: {
      struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
      b->shader->info.inputs_read |= BITFIELD64_BIT(io_semantics.location);

      if (io_semantics.location != VARYING_SLOT_POS)
         return false;

      unsigned component = nir_intrinsic_component(intr);
      data->fs.uses.z |= (component == 2);
      data->fs.uses.w |= (component == 3);
      break;
   }

   case nir_intrinsic_load_blend_const_color_rgba:
      data->fs.blend_consts_needed |= PIPE_MASK_RGBA;
      break;

   case nir_intrinsic_load_front_face_op_pco:
      BITSET_SET(b->shader->info.system_values_read, SYSTEM_VALUE_FRONT_FACE);
      break;

   default:
      break;
   }

   return false;
}

/**
 * \brief Gathers fragment shader data.
 *
 * \param[in] nir NIR shader.
 * \param[in,out] data Shader data.
 */
static void gather_fs_data(nir_shader *nir, pco_data *data)
{
   nir_shader_intrinsics_pass(nir, gather_fs_data_pass, nir_metadata_all, data);

   /* If any inputs use smooth shading, then w is needed. */
   if (!data->fs.uses.w) {
      nir_foreach_shader_in_variable (var, nir) {
         if (var->data.interpolation > INTERP_MODE_SMOOTH)
            continue;

         data->fs.uses.w = true;
         break;
      }
   }

   data->fs.uses.fbfetch = nir->info.fs.uses_fbfetch_output;
   data->fs.uses.fbfetch |= data->fs.meta_present.color_write_enable;

   data->fs.uses.early_frag = nir->info.fs.early_fragment_tests;
   data->fs.uses.sample_shading |= nir->info.fs.uses_sample_shading;
}

/**
 * \brief Gathers vertex shader data.
 *
 * \param[in] nir NIR shader.
 * \param[in,out] data Shader data.
 */
static void gather_vs_data(nir_shader *nir, pco_data *data)
{
   pco_vs_data *vs_data = &data->vs;

   vs_data->clip_count = nir->info.clip_distance_array_size;
   vs_data->cull_count = nir->info.cull_distance_array_size;
}

/**
 * \brief Gathers compute shader data.
 *
 * \param[in] nir NIR shader.
 * \param[in,out] data Shader data.
 */
static void gather_cs_data(nir_shader *nir, pco_data *data)
{
   for (unsigned u = 0; u < ARRAY_SIZE(data->cs.workgroup_size); ++u)
      data->cs.workgroup_size[u] = nir->info.workgroup_size[u];
}

/**
 * \brief Checks whether a NIR intrinsic op is atomic.
 *
 * \param[in] op The NIR intrinsic op.
 * \return True if the intrinsic op is atomic, else false.
 */
/* TODO: what about emulated atomic ops? */
static inline bool intr_op_is_atomic(nir_intrinsic_op op)
{
   switch (op) {
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      return true;

   default:
      break;
   }
   return false;
}

static void gather_common_store_data(nir_intrinsic_instr *intr,
                                     pco_common_data *common)
{
   nir_src *offset_src;
   unsigned num_components;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_push_constant:
      offset_src = &intr->src[0];
      num_components = intr->def.num_components;
      break;

   default:
      return;
   }

   if (nir_src_is_const(*offset_src) && common->push_consts.used != ~0U) {
      unsigned offset = nir_src_as_uint(*offset_src);
      common->push_consts.used =
         MAX2(common->push_consts.used, offset + num_components);
   } else {
      common->push_consts.used = ~0U;
   }
}

/**
 * \brief Gather common data pass.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in,out] cb_data Callback data.
 * \return True if the shader was modified (always return false).
 */
static bool gather_common_data_pass(UNUSED struct nir_builder *b,
                                    nir_intrinsic_instr *intr,
                                    void *cb_data)
{
   pco_data *data = cb_data;
   data->common.uses.atomics |= intr_op_is_atomic(intr->intrinsic);
   gather_common_store_data(intr, &data->common);

   return false;
}

/**
 * \brief Gathers data common to all shader stages.
 *
 * \param[in] nir NIR shader.
 * \param[in,out] data Shader data.
 */
static void gather_common_data(nir_shader *nir, pco_data *data)
{
   nir_shader_intrinsics_pass(nir,
                              gather_common_data_pass,
                              nir_metadata_all,
                              data);

   data->common.scratch = nir->scratch_size;
}

/**
 * \brief Gathers shader data.
 *
 * \param[in] nir NIR shader.
 * \param[in,out] data Shader data.
 */
static void gather_data(nir_shader *nir, pco_data *data)
{
   gather_common_data(nir, data);

   switch (nir->info.stage) {
   case MESA_SHADER_FRAGMENT:
      return gather_fs_data(nir, data);

   case MESA_SHADER_VERTEX:
      return gather_vs_data(nir, data);

   case MESA_SHADER_COMPUTE:
      return gather_cs_data(nir, data);

   default:
      break;
   }

   UNREACHABLE("");
}

static bool should_vectorize_mem_cb(unsigned align_mul,
                                    unsigned align_offset,
                                    unsigned bit_size,
                                    unsigned num_components,
                                    int64_t hole_size,
                                    nir_intrinsic_instr *low,
                                    nir_intrinsic_instr *high,
                                    void *data)
{
   if (bit_size > 32 || hole_size > 0)
      return false;

   if (!nir_num_components_valid(num_components))
      return false;

   return true;
}

static void pco_nir_opt(pco_ctx *ctx, nir_shader *nir, bool algebraic)
{
   bool progress;

   unsigned count = 0;
   do {
      progress = false;

      if (count > 1000) {
         printf("WARNING! Infinite opt loop!\n");
         break;
      }

      NIR_PASS(progress, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_opt_deref);

      bool progress_opt_memcpy = false;
      NIR_PASS(progress_opt_memcpy, nir, nir_opt_memcpy);
      progress |= progress_opt_memcpy;

      if (progress_opt_memcpy)
         NIR_PASS(progress, nir, nir_split_var_copies);

      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      if (!nir->info.var_copies_lowered)
         NIR_PASS(progress, nir, nir_opt_find_array_copies);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_if, nir_opt_if_optimize_phi_true_false);
      nir_opt_peephole_select_options peep_opts = {
         .limit = 64,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peep_opts);

      NIR_PASS(progress, nir, nir_lower_int64);

      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_lower_alu);
      NIR_PASS(progress, nir, nir_lower_pack);

      if (algebraic) {
         NIR_PASS(progress, nir, nir_opt_algebraic);
         NIR_PASS(progress, nir, pco_nir_lower_algebraic);
      }

      NIR_PASS(progress, nir, nir_opt_constant_folding);

      nir_load_store_vectorize_options vectorize_opts = {
         .modes = nir_var_mem_ubo | nir_var_mem_ssbo | nir_var_mem_global,
         .callback = should_vectorize_mem_cb,
      };
      NIR_PASS(progress, nir, nir_opt_load_store_vectorize, &vectorize_opts);

      NIR_PASS(progress, nir, nir_opt_shrink_stores, false);
      NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);

      NIR_PASS(progress, nir, nir_opt_loop);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
   } while (progress);
}

static bool check_mem_writes(nir_builder *b,
                             nir_intrinsic_instr *intr,
                             UNUSED void *cb_data)
{
   b->shader->info.writes_memory |= nir_intrinsic_writes_external_memory(intr);
   return false;
}

/**
 * \brief Runs pre-processing passes on a NIR shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] nir NIR shader.
 */
void pco_preprocess_nir(pco_ctx *ctx, nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      nir_shader_intrinsics_pass(nir, check_mem_writes, nir_metadata_all, NULL);

   if (nir->info.stage == MESA_SHADER_COMPUTE)
      NIR_PASS(_, nir, pco_nir_compute_instance_check);

   if (nir->info.internal)
      NIR_PASS(_, nir, nir_lower_returns);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
         .frag_coord = true,
         .point_coord = true,
      };
      NIR_PASS(_, nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);
      NIR_PASS(_, nir, nir_lower_helper_writes, true);
      NIR_PASS(_, nir, nir_lower_is_helper_invocation);
      NIR_PASS(_, nir, nir_lower_terminate_to_demote);
      NIR_PASS(_, nir, nir_lower_halt_to_return);
      NIR_PASS(_, nir, nir_lower_returns);
   }

   NIR_PASS(_, nir, nir_lower_system_values);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS(_,
               nir,
               nir_lower_compute_system_values,
               &(nir_lower_compute_system_values_options){
                  .lower_cs_local_id_to_index = true,
               });
   }

   NIR_PASS(_, nir, pco_nir_lower_subgroups);

   NIR_PASS(_,
            nir,
            nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir),
            nir_var_shader_out | nir_var_shader_in);

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_split_per_member_structs);
   NIR_PASS(_,
            nir,
            nir_split_struct_vars,
            nir_var_function_temp | nir_var_shader_temp);
   NIR_PASS(_,
            nir,
            nir_split_array_vars,
            nir_var_function_temp | nir_var_shader_temp);

   pco_nir_opt(ctx, nir, true);

   NIR_PASS(_,
            nir,
            nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_shader_in | nir_var_shader_out,
            UINT32_MAX);

   NIR_PASS(_, nir, nir_lower_vars_to_ssa);

   if (!nir->info.internal) {
      /* TODO: test with different size_threshold values. */
      NIR_PASS(_,
               nir,
               nir_lower_vars_to_scratch,
               nir_var_function_temp,
               8,
               glsl_get_natural_size_align_bytes,
               glsl_get_word_size_align_bytes);
   }

   NIR_PASS(_,
            nir,
            nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_function_temp,
            UINT32_MAX);

   pco_nir_opt(ctx, nir, true);
   NIR_PASS(_, nir, nir_opt_idiv_const, 32);
   NIR_PASS(_,
            nir,
            nir_lower_idiv,
            &(nir_lower_idiv_options){
               .allow_fp16 = false,
            });

   NIR_PASS(_, nir, nir_scale_fdiv);
   NIR_PASS(_, nir, nir_lower_frexp);
   NIR_PASS(_, nir, nir_lower_flrp, 32, true);

   NIR_PASS(_, nir, nir_remove_dead_derefs);
   NIR_PASS(_, nir, nir_opt_undef);
   NIR_PASS(_, nir, nir_lower_undef_to_zero);
   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_,
            nir,
            nir_remove_dead_variables,
            nir_var_function_temp | nir_var_shader_temp,
            NULL);

   /* Fold constant offset srcs for IO. */
   NIR_PASS(_,
            nir,
            nir_opt_constant_folding);

   NIR_PASS(_,
            nir,
            nir_lower_io_array_vars_to_elements_no_indirects,
            nir->info.stage == MESA_SHADER_VERTEX);

   pco_nir_opt(ctx, nir, true);

   if (pco_should_print_nir(nir)) {
      puts("after pco_preprocess_nir:");
      nir_print_shader(nir, stdout);
   }
}

static bool can_remove_var(nir_variable *var, UNUSED void *data)
{
   return !var->data.always_active_io;
}

/**
 * \brief Performs linking optimizations on consecutive NIR shader stages.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] producer NIR producer shader.
 * \param[in,out] consumer NIR consumer shader.
 * \param[in,out] producer_data Producer shader data.
 * \param[in,out] consumer_data Consumer shader data.
 */
void pco_link_nir(pco_ctx *ctx,
                  nir_shader *producer,
                  nir_shader *consumer,
                  pco_data *producer_data,
                  pco_data *consumer_data)
{
   pco_nir_link_multiview(producer, consumer, consumer_data);
   pco_nir_link_clip_cull_vars(producer, consumer);

   nir_lower_io_array_vars_to_elements(producer, consumer);
   nir_validate_shader(producer, "after nir_lower_io_array_vars_to_elements");
   nir_validate_shader(consumer, "after nir_lower_io_array_vars_to_elements");

   NIR_PASS(_, producer, nir_lower_io_vars_to_scalar, nir_var_shader_out);
   NIR_PASS(_, consumer, nir_lower_io_vars_to_scalar, nir_var_shader_in);

   pco_nir_opt(ctx, producer, true);
   pco_nir_opt(ctx, consumer, true);

   if (nir_link_opt_varyings(producer, consumer))
      pco_nir_opt(ctx, consumer, true);

   nir_remove_dead_variables_options rdv = {
      .can_remove_var = can_remove_var,
   };

   NIR_PASS(_, producer, nir_remove_dead_variables, nir_var_shader_out, &rdv);
   NIR_PASS(_, consumer, nir_remove_dead_variables, nir_var_shader_in, &rdv);

   bool progress = nir_remove_unused_varyings(producer, consumer);
   nir_compact_varyings(producer, consumer, true);

   if (progress) {
      NIR_PASS(_, producer, nir_lower_global_vars_to_local);
      NIR_PASS(_, consumer, nir_lower_global_vars_to_local);

      NIR_PASS(_,
               producer,
               nir_lower_indirect_derefs_to_if_else_trees,
               nir_var_shader_in | nir_var_shader_out,
               UINT32_MAX);
      NIR_PASS(_,
               consumer,
               nir_lower_indirect_derefs_to_if_else_trees,
               nir_var_shader_in | nir_var_shader_out,
               UINT32_MAX);

      pco_nir_opt(ctx, producer, true);
      pco_nir_opt(ctx, consumer, true);
   }

   NIR_PASS(_, producer, nir_opt_vectorize_io_vars, nir_var_shader_out);
   NIR_PASS(_, producer, nir_opt_combine_stores, nir_var_shader_out);
   NIR_PASS(_, consumer, nir_opt_vectorize_io_vars, nir_var_shader_in);

   if (pco_should_print_nir(producer)) {
      puts("producer after pco_link_nir:");
      nir_print_shader(producer, stdout);
   }

   if (pco_should_print_nir(consumer)) {
      puts("consumer after pco_link_nir:");
      nir_print_shader(consumer, stdout);
   }
}

/**
 * \brief Performs reverse linking optimizations on consecutive NIR shader
 * stages.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] producer NIR producer shader.
 * \param[in,out] consumer NIR consumer shader.
 */
void pco_rev_link_nir(pco_ctx *ctx, nir_shader *producer, nir_shader *consumer)
{
   /* Propagate back/adjust the interpolation qualifiers. */
   nir_foreach_shader_in_variable (in_var, consumer) {
      if (in_var->data.location == VARYING_SLOT_POS ||
          in_var->data.location == VARYING_SLOT_PNTC) {
         in_var->data.interpolation = INTERP_MODE_NOPERSPECTIVE;
      } else if (in_var->data.interpolation == INTERP_MODE_NONE) {
         in_var->data.interpolation = INTERP_MODE_SMOOTH;
      }

      nir_foreach_shader_out_variable (out_var, producer) {
         if (!varyings_match(out_var, in_var))
            continue;

         out_var->data.interpolation = in_var->data.interpolation;
         break;
      }
   }

   if (pco_should_print_nir(producer)) {
      puts("producer after pco_rev_link_nir:");
      nir_print_shader(producer, stdout);
   }

   if (pco_should_print_nir(consumer)) {
      puts("consumer after pco_rev_link_nir:");
      nir_print_shader(consumer, stdout);
   }
}

static bool robustness_filter(const nir_intrinsic_instr *intr,
                              UNUSED const void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      return true;

   default:
      break;
   }

   return false;
}

/**
 * \brief Lowers a NIR shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] nir NIR shader.
 * \param[in,out] data Shader data.
 */
void pco_lower_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data)
{
   NIR_PASS(_,
            nir,
            nir_opt_access,
            &(nir_opt_access_options){ .is_vulkan = true });

   NIR_PASS(_, nir, nir_opt_barrier_modes);
   NIR_PASS(_, nir, nir_opt_combine_barriers, NULL, NULL);
   NIR_PASS(_, nir, pco_nir_lower_barriers, data);

   NIR_PASS(_, nir, nir_lower_memory_model);

   NIR_PASS(_, nir, nir_opt_licm);

   NIR_PASS(_, nir, nir_lower_memcpy);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_opt_vectorize_io_vars, nir_var_shader_out);

   NIR_PASS(_,
            nir,
            nir_lower_explicit_io,
            nir_var_mem_ubo | nir_var_mem_ssbo,
            nir_address_format_vec2_index_32bit_offset);

   NIR_PASS(_,
            nir,
            nir_lower_explicit_io,
            nir_var_mem_global,
            nir_address_format_2x32bit_global);

   nir_move_options move_options = nir_move_load_global | nir_move_load_ubo |
                                   nir_move_load_ssbo | nir_move_load_input |
                                   nir_move_load_frag_coord |
                                   nir_intrinsic_load_uniform;
   NIR_PASS(_, nir, nir_opt_sink, move_options);
   NIR_PASS(_, nir, nir_opt_move, move_options);

   if (!nir->info.shared_memory_explicit_layout) {
      NIR_PASS(_,
               nir,
               nir_lower_vars_to_explicit_types,
               nir_var_mem_shared,
               shared_var_info);
   }

   NIR_PASS(_,
            nir,
            nir_lower_explicit_io,
            nir_var_mem_push_const | nir_var_mem_shared,
            nir_address_format_32bit_offset);
   NIR_PASS(_,
            nir,
            nir_lower_io_to_scalar,
            nir_var_mem_push_const | nir_var_mem_shared,
            NULL,
            NULL);

   if (data->common.robust_buffer_access)
      NIR_PASS(_, nir, nir_lower_robust_access, robustness_filter, NULL);

   if (data->common.null_descriptor) {
      NIR_PASS(_,
               nir,
               pco_nir_lower_null_descriptors,
               pco_nir_lower_null_descriptor_all);
   }

   NIR_PASS(_, nir, pco_nir_lower_vk, data);
   NIR_PASS(_, nir, pco_nir_lower_io);

   NIR_PASS(_, nir, nir_opt_constant_folding);

   NIR_PASS(_,
            nir,
            nir_lower_io,
            nir_var_shader_in | nir_var_shader_out,
            glsl_type_size,
            nir_lower_io_lower_64bit_to_32 |
               nir_lower_io_use_interpolated_input_intrinsics);

   nir_variable_mode vec_modes = nir->info.stage == MESA_SHADER_FRAGMENT
                                    ? nir_var_shader_out
                                    : nir_var_shader_in;
   NIR_PASS(_, nir, nir_lower_io_to_scalar, vec_modes, NULL, NULL);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_vectorize_io, vec_modes, false);

   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_opt_constant_folding);

   /* Internal shaders will be using invalid32 types at this stage. */
   if (!nir->info.internal)
      NIR_PASS(_, nir, nir_unlower_io_to_vars, true);

   if (nir->info.stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, pco_nir_lower_clip_cull_vars);

   NIR_PASS(_, nir, pco_nir_lower_images, data);
   NIR_PASS(_, nir, pco_nir_lower_atomics, data);
   NIR_PASS(_,
            nir,
            nir_lower_tex,
            &(nir_lower_tex_options){
               .lower_txd_cube_map = true,
            });
   NIR_PASS(_, nir, pco_nir_lower_tex, data, ctx);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, pco_nir_lower_alpha_to_coverage);

      NIR_PASS(_, nir, nir_lower_blend, &data->fs.blend_opts);

      nir_opt_peephole_select_options peep_opts = {
         .limit = 0,
         .discard_ok = true,
      };
      NIR_PASS(_, nir, nir_opt_peephole_select, &peep_opts);
      NIR_PASS(_, nir, pco_nir_lower_interpolation, &data->fs);
      NIR_PASS(_, nir, pco_nir_pfo, &data->fs);
      NIR_PASS(_, nir, pco_nir_lower_fs_intrinsics);
   } else if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_,
               nir,
               nir_lower_point_size,
               PVR_POINT_SIZE_RANGE_MIN,
               PVR_POINT_SIZE_RANGE_MAX,
               nir_type_invalid);

      if (!nir->info.internal)
         NIR_PASS(_, nir, pco_nir_point_size);

      NIR_PASS(_, nir, pco_nir_pvi, &data->vs);
      NIR_PASS(_, nir, pco_nir_lower_vs_intrinsics);
   }

   if (data->common.uses.usclib) {
      assert(ctx->usclib);

      nir_link_shader_functions(nir, ctx->usclib);
      NIR_PASS(_, nir, nir_inline_functions);
      nir_remove_non_entrypoints(nir);
      NIR_PASS(_, nir, nir_opt_deref);
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);
      NIR_PASS(_, nir, nir_remove_dead_derefs);
      NIR_PASS(_,
               nir,
               nir_remove_dead_variables,
               nir_var_function_temp | nir_var_shader_temp,
               NULL);
      NIR_PASS(_,
               nir,
               nir_lower_vars_to_explicit_types,
               nir_var_shader_temp | nir_var_function_temp,
               glsl_get_cl_type_size_align);

      NIR_PASS(_, nir, nir_opt_idiv_const, 32);
      NIR_PASS(_,
               nir,
               nir_lower_idiv,
               &(nir_lower_idiv_options){
                  .allow_fp16 = false,
               });
   }

   NIR_PASS(_,
            nir,
            nir_lower_io_to_scalar,
            nir_var_shader_in | nir_var_shader_out,
            NULL,
            NULL);

   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_opt_copy_prop_vars);
   NIR_PASS(_, nir, nir_opt_dead_write_vars);
   NIR_PASS(_, nir, nir_opt_combine_stores, nir_var_all);

   pco_nir_opt(ctx, nir, true);

   vec_modes = nir_var_shader_in;
   /* Fragment shader needs scalar writes after pfo. */
   if (nir->info.stage != MESA_SHADER_FRAGMENT)
      vec_modes |= nir_var_shader_out;

   NIR_PASS(_, nir, nir_opt_vectorize_io, vec_modes, false);

   /* Special case for frag coords:
    * - x,y come from (non-consecutive) special regs - always scalar.
    * - z,w are iterated and driver will make sure they're consecutive.
    *   - TODO: keep scalar for now, but add pass to vectorize.
    */
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_,
               nir,
               nir_lower_io_to_scalar,
               nir_var_shader_in,
               frag_in_scalar_filter,
               nir);
   }

   pco_nir_opt(ctx, nir, true);

   if (pco_should_print_nir(nir)) {
      puts("after pco_lower_nir:");
      nir_print_shader(nir, stdout);
   }
}

static bool is_phi_with_undefs(const nir_instr *instr,
                               UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_phi)
      return false;

   nir_phi_instr *phi = nir_instr_as_phi(instr);

   nir_foreach_phi_src (phi_src, phi) {
      if (nir_src_is_undef(phi_src->src))
         return true;
   }

   return false;
}

static nir_def *
lower_phi_with_undefs(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   nir_phi_instr *phi = nir_instr_as_phi(instr);

   nir_foreach_phi_src (phi_src, phi) {
      if (nir_src_is_undef(phi_src->src)) {
         b->cursor = nir_after_block(phi_src->pred);
         nir_src_rewrite(&phi_src->src,
                         nir_imm_intN_t(b, 0, phi_src->src.ssa->bit_size));
      }
   }

   return NIR_LOWER_INSTR_PROGRESS;
}

static bool
remat_load_const(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   if (instr->type != nir_instr_type_load_const)
      return false;

   nir_load_const_instr *nconst = nir_instr_as_load_const(instr);

   if (list_is_singular(&nconst->def.uses))
      return false;

   nir_foreach_use_safe (src, &nconst->def) {
      nir_instr *use_instr = nir_src_parent_instr(src);
      b->cursor = nir_before_instr(use_instr);

      nir_def *remat_const = nir_build_imm(b,
                                           nconst->def.num_components,
                                           nconst->def.bit_size,
                                           nconst->value);

      nir_src_rewrite(src, remat_const);
   }

   nir_instr_remove(instr);

   return true;
}
/**
 * \brief Runs post-processing passes on a NIR shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] nir NIR shader.
 * \param[in,out] data Shader data.
 */
void pco_postprocess_nir(pco_ctx *ctx, nir_shader *nir, pco_data *data)
{
   nir_move_options move_options = nir_move_const_undef | nir_move_copies |
                                   nir_move_comparisons | nir_move_alu;
   NIR_PASS(_, nir, nir_opt_sink, move_options);
   NIR_PASS(_, nir, nir_opt_move, move_options);

   bool progress;
   do {
      progress = false;

      NIR_PASS(_, nir, nir_opt_algebraic_late);
      NIR_PASS(_, nir, pco_nir_lower_algebraic_late);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_opt_cse);
   } while (progress);

   pco_nir_opt(ctx, nir, false);

   NIR_PASS(_, nir, nir_lower_all_phis_to_scalar);

   /* Temporary: lower phi undefs to zero because at this stage we don't want to
    * lower *all* undefs to zero, but still want to avoid undefined behaviour...
    */
   nir_shader_lower_instructions(nir,
                                 is_phi_with_undefs,
                                 lower_phi_with_undefs,
                                 NULL);

   NIR_PASS(_, nir, nir_convert_from_ssa, true, false);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_move_vec_src_uses_to_dest, false);
   NIR_PASS(_, nir, nir_opt_dce);

   progress = false;
   NIR_PASS(progress, nir, nir_opt_rematerialize_compares);
   if (progress)
      NIR_PASS(_, nir, nir_opt_dce);

   NIR_PASS(_, nir, nir_trivialize_registers);

   if (!nir->info.internal) {
      nir_shader_instructions_pass(nir,
                                   remat_load_const,
                                   nir_metadata_none,
                                   NULL);
   }

   /* Re-index everything. */
   nir_foreach_function_with_impl (_, impl, nir) {
      nir_index_blocks(impl);
      nir_index_instrs(impl);
      nir_index_ssa_defs(impl);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   gather_data(nir, data);

   if (pco_should_print_nir(nir)) {
      puts("after pco_postprocess_nir:");
      nir_print_shader(nir, stdout);
   }
}
