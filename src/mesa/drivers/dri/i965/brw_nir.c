/*
 * Copyright © 2014 Intel Corporation
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

#include "brw_nir.h"
#include "brw_shader.h"
#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"

static bool
is_input(nir_intrinsic_instr *intrin)
{
   return intrin->intrinsic == nir_intrinsic_load_input ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_input;
}

static bool
is_output(nir_intrinsic_instr *intrin)
{
   return intrin->intrinsic == nir_intrinsic_load_output ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_store_output ||
          intrin->intrinsic == nir_intrinsic_store_per_vertex_output;
}

/**
 * In many cases, we just add the base and offset together, so there's no
 * reason to keep them separate.  Sometimes, combining them is essential:
 * if a shader only accesses part of a compound variable (such as a matrix
 * or array), the variable's base may not actually exist in the VUE map.
 *
 * This pass adds constant offsets to instr->const_index[0], and resets
 * the offset source to 0.  Non-constant offsets remain unchanged - since
 * we don't know what part of a compound variable is accessed, we allocate
 * storage for the entire thing.
 */

static bool
add_const_offset_to_base_block(nir_block *block, nir_builder *b,
                               nir_variable_mode mode)
{
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if ((mode == nir_var_shader_in && is_input(intrin)) ||
          (mode == nir_var_shader_out && is_output(intrin))) {
         nir_src *offset = nir_get_io_offset_src(intrin);
         nir_const_value *const_offset = nir_src_as_const_value(*offset);

         if (const_offset) {
            intrin->const_index[0] += const_offset->u32[0];
            b->cursor = nir_before_instr(&intrin->instr);
            nir_instr_rewrite_src(&intrin->instr, offset,
                                  nir_src_for_ssa(nir_imm_int(b, 0)));
         }
      }
   }
   return true;
}

static void
add_const_offset_to_base(nir_shader *nir, nir_variable_mode mode)
{
   nir_foreach_function(f, nir) {
      if (f->impl) {
         nir_builder b;
         nir_builder_init(&b, f->impl);
         nir_foreach_block(block, f->impl) {
            add_const_offset_to_base_block(block, &b, mode);
         }
      }
   }
}

static bool
remap_vs_attrs(nir_block *block, struct nir_shader_info *nir_info)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (intrin->intrinsic == nir_intrinsic_load_input) {
         /* Attributes come in a contiguous block, ordered by their
          * gl_vert_attrib value.  That means we can compute the slot
          * number for an attribute by masking out the enabled attributes
          * before it and counting the bits.
          */
         int attr = intrin->const_index[0];
         int slot = _mesa_bitcount_64(nir_info->inputs_read &
                                      BITFIELD64_MASK(attr));
         int dslot = _mesa_bitcount_64(nir_info->double_inputs_read &
                                       BITFIELD64_MASK(attr));
         intrin->const_index[0] = 4 * (slot + dslot);
      }
   }
   return true;
}

static bool
remap_inputs_with_vue_map(nir_block *block, const struct brw_vue_map *vue_map)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (intrin->intrinsic == nir_intrinsic_load_input ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_input) {
         int vue_slot = vue_map->varying_to_slot[intrin->const_index[0]];
         assert(vue_slot != -1);
         intrin->const_index[0] = vue_slot;
      }
   }
   return true;
}

static bool
remap_patch_urb_offsets(nir_block *block, nir_builder *b,
                        const struct brw_vue_map *vue_map)
{
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      gl_shader_stage stage = b->shader->stage;

      if ((stage == MESA_SHADER_TESS_CTRL && is_output(intrin)) ||
          (stage == MESA_SHADER_TESS_EVAL && is_input(intrin))) {
         int vue_slot = vue_map->varying_to_slot[intrin->const_index[0]];
         assert(vue_slot != -1);
         intrin->const_index[0] = vue_slot;

         nir_src *vertex = nir_get_io_vertex_index_src(intrin);
         if (vertex) {
            nir_const_value *const_vertex = nir_src_as_const_value(*vertex);
            if (const_vertex) {
               intrin->const_index[0] += const_vertex->u32[0] *
                                         vue_map->num_per_vertex_slots;
            } else {
               b->cursor = nir_before_instr(&intrin->instr);

               /* Multiply by the number of per-vertex slots. */
               nir_ssa_def *vertex_offset =
                  nir_imul(b,
                           nir_ssa_for_src(b, *vertex, 1),
                           nir_imm_int(b,
                                       vue_map->num_per_vertex_slots));

               /* Add it to the existing offset */
               nir_src *offset = nir_get_io_offset_src(intrin);
               nir_ssa_def *total_offset =
                  nir_iadd(b, vertex_offset,
                           nir_ssa_for_src(b, *offset, 1));

               nir_instr_rewrite_src(&intrin->instr, offset,
                                     nir_src_for_ssa(total_offset));
            }
         }
      }
   }
   return true;
}

void
brw_nir_lower_vs_inputs(nir_shader *nir,
                        const struct brw_device_info *devinfo,
                        bool is_scalar,
                        bool use_legacy_snorm_formula,
                        const uint8_t *vs_attrib_wa_flags)
{
   /* Start with the location of the variable's base. */
   foreach_list_typed(nir_variable, var, node, &nir->inputs) {
      var->data.driver_location = var->data.location;
   }

   /* Now use nir_lower_io to walk dereference chains.  Attribute arrays are
    * loaded as one vec4 or dvec4 per element (or matrix column), depending on
    * whether it is a double-precision type or not.
    */
   nir_lower_io(nir, nir_var_shader_in, type_size_vs_input);

   /* This pass needs actual constants */
   nir_opt_constant_folding(nir);

   add_const_offset_to_base(nir, nir_var_shader_in);

   brw_nir_apply_attribute_workarounds(nir, use_legacy_snorm_formula,
                                       vs_attrib_wa_flags);

   if (is_scalar) {
      /* Finally, translate VERT_ATTRIB_* values into the actual registers. */

      nir_foreach_function(function, nir) {
         if (function->impl) {
            nir_foreach_block(block, function->impl) {
               remap_vs_attrs(block, &nir->info);
            }
         }
      }
   }
}

void
brw_nir_lower_vue_inputs(nir_shader *nir, bool is_scalar,
                         const struct brw_vue_map *vue_map)
{
   foreach_list_typed(nir_variable, var, node, &nir->inputs) {
      var->data.driver_location = var->data.location;
   }

   /* Inputs are stored in vec4 slots, so use type_size_vec4(). */
   nir_lower_io(nir, nir_var_shader_in, type_size_vec4);

   if (is_scalar || nir->stage != MESA_SHADER_GEOMETRY) {
      /* This pass needs actual constants */
      nir_opt_constant_folding(nir);

      add_const_offset_to_base(nir, nir_var_shader_in);

      nir_foreach_function(function, nir) {
         if (function->impl) {
            nir_foreach_block(block, function->impl) {
               remap_inputs_with_vue_map(block, vue_map);
            }
         }
      }
   }
}

void
brw_nir_lower_tes_inputs(nir_shader *nir, const struct brw_vue_map *vue_map)
{
   foreach_list_typed(nir_variable, var, node, &nir->inputs) {
      var->data.driver_location = var->data.location;
   }

   nir_lower_io(nir, nir_var_shader_in, type_size_vec4);

   /* This pass needs actual constants */
   nir_opt_constant_folding(nir);

   add_const_offset_to_base(nir, nir_var_shader_in);

   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);
         nir_foreach_block(block, function->impl) {
            remap_patch_urb_offsets(block, &b, vue_map);
         }
      }
   }
}

void
brw_nir_lower_fs_inputs(nir_shader *nir)
{
   nir_assign_var_locations(&nir->inputs, &nir->num_inputs, type_size_scalar);
   nir_lower_io(nir, nir_var_shader_in, type_size_scalar);
}

void
brw_nir_lower_vue_outputs(nir_shader *nir,
                          bool is_scalar)
{
   if (is_scalar) {
      nir_assign_var_locations(&nir->outputs, &nir->num_outputs,
                               type_size_vec4_times_4);
      nir_lower_io(nir, nir_var_shader_out, type_size_vec4_times_4);
   } else {
      nir_foreach_variable(var, &nir->outputs)
         var->data.driver_location = var->data.location;
      nir_lower_io(nir, nir_var_shader_out, type_size_vec4);
   }
}

void
brw_nir_lower_tcs_outputs(nir_shader *nir, const struct brw_vue_map *vue_map)
{
   nir_foreach_variable(var, &nir->outputs) {
      var->data.driver_location = var->data.location;
   }

   nir_lower_io(nir, nir_var_shader_out, type_size_vec4);

   /* This pass needs actual constants */
   nir_opt_constant_folding(nir);

   add_const_offset_to_base(nir, nir_var_shader_out);

   nir_foreach_function(function, nir) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);
         nir_foreach_block(block, function->impl) {
            remap_patch_urb_offsets(block, &b, vue_map);
         }
      }
   }
}

void
brw_nir_lower_fs_outputs(nir_shader *nir)
{
   nir_assign_var_locations(&nir->outputs, &nir->num_outputs,
                            type_size_scalar);
   nir_lower_io(nir, nir_var_shader_out, type_size_scalar);
}

void
brw_nir_lower_cs_shared(nir_shader *nir)
{
   nir_assign_var_locations(&nir->shared, &nir->num_shared,
                            type_size_scalar_bytes);
   nir_lower_io(nir, nir_var_shared, type_size_scalar_bytes);
}

#define OPT(pass, ...) ({                                  \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   if (this_progress)                                      \
      progress = true;                                     \
   this_progress;                                          \
})

#define OPT_V(pass, ...) NIR_PASS_V(nir, pass, ##__VA_ARGS__)

static nir_shader *
nir_optimize(nir_shader *nir, bool is_scalar)
{
   bool progress;
   do {
      progress = false;
      OPT_V(nir_lower_vars_to_ssa);

      if (is_scalar) {
         OPT_V(nir_lower_alu_to_scalar);
      }

      OPT(nir_copy_prop);

      if (is_scalar) {
         OPT_V(nir_lower_phis_to_scalar);
      }

      OPT(nir_copy_prop);
      OPT(nir_opt_dce);
      OPT(nir_opt_cse);
      OPT(nir_opt_peephole_select);
      OPT(nir_opt_algebraic);
      OPT(nir_opt_constant_folding);
      OPT(nir_opt_dead_cf);
      OPT(nir_opt_remove_phis);
      OPT(nir_opt_undef);
      OPT_V(nir_lower_doubles, nir_lower_drcp |
                               nir_lower_dsqrt |
                               nir_lower_drsq |
                               nir_lower_dtrunc |
                               nir_lower_dfloor |
                               nir_lower_dceil |
                               nir_lower_dfract |
                               nir_lower_dround_even |
                               nir_lower_dmod);
      OPT_V(nir_lower_double_pack);
   } while (progress);

   return nir;
}

/* Does some simple lowering and runs the standard suite of optimizations
 *
 * This is intended to be called more-or-less directly after you get the
 * shader out of GLSL or some other source.  While it is geared towards i965,
 * it is not at all generator-specific except for the is_scalar flag.  Even
 * there, it is safe to call with is_scalar = false for a shader that is
 * intended for the FS backend as long as nir_optimize is called again with
 * is_scalar = true to scalarize everything prior to code gen.
 */
nir_shader *
brw_preprocess_nir(const struct brw_compiler *compiler, nir_shader *nir)
{
   bool progress; /* Written by OPT and OPT_V */
   (void)progress;

   const bool is_scalar = compiler->scalar_stage[nir->stage];

   if (nir->stage == MESA_SHADER_GEOMETRY)
      OPT(nir_lower_gs_intrinsics);

   if (compiler->precise_trig)
      OPT(brw_nir_apply_trig_workarounds);

   static const nir_lower_tex_options tex_options = {
      .lower_txp = ~0,
   };

   OPT(nir_lower_tex, &tex_options);
   OPT(nir_normalize_cubemap_coords);

   OPT(nir_lower_global_vars_to_local);

   OPT(nir_split_var_copies);

   nir = nir_optimize(nir, is_scalar);

   if (is_scalar) {
      OPT_V(nir_lower_load_const_to_scalar);
   }

   /* Lower a bunch of stuff */
   OPT_V(nir_lower_var_copies);

   /* Get rid of split copies */
   nir = nir_optimize(nir, is_scalar);

   OPT(nir_remove_dead_variables, nir_var_local);

   return nir;
}

/* Prepare the given shader for codegen
 *
 * This function is intended to be called right before going into the actual
 * backend and is highly backend-specific.  Also, once this function has been
 * called on a shader, it will no longer be in SSA form so most optimizations
 * will not work.
 */
nir_shader *
brw_postprocess_nir(nir_shader *nir,
                    const struct brw_device_info *devinfo,
                    bool is_scalar)
{
   bool debug_enabled =
      (INTEL_DEBUG & intel_debug_flag_for_shader_stage(nir->stage));

   bool progress; /* Written by OPT and OPT_V */
   (void)progress;

   nir = nir_optimize(nir, is_scalar);

   if (devinfo->gen >= 6) {
      /* Try and fuse multiply-adds */
      OPT(brw_nir_opt_peephole_ffma);
   }

   OPT(nir_opt_algebraic_late);

   OPT(nir_lower_locals_to_regs);

   OPT_V(nir_lower_to_source_mods);
   OPT(nir_copy_prop);
   OPT(nir_opt_dce);

   if (unlikely(debug_enabled)) {
      /* Re-index SSA defs so we print more sensible numbers. */
      nir_foreach_function(function, nir) {
         if (function->impl)
            nir_index_ssa_defs(function->impl);
      }

      fprintf(stderr, "NIR (SSA form) for %s shader:\n",
              _mesa_shader_stage_to_string(nir->stage));
      nir_print_shader(nir, stderr);
   }

   OPT_V(nir_convert_from_ssa, true);

   if (!is_scalar) {
      OPT_V(nir_move_vec_src_uses_to_dest);
      OPT(nir_lower_vec_to_movs);
   }

   /* This is the last pass we run before we start emitting stuff.  It
    * determines when we need to insert boolean resolves on Gen <= 5.  We
    * run it last because it stashes data in instr->pass_flags and we don't
    * want that to be squashed by other NIR passes.
    */
   if (devinfo->gen <= 5)
      brw_nir_analyze_boolean_resolves(nir);

   nir_sweep(nir);

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "NIR (final form) for %s shader:\n",
              _mesa_shader_stage_to_string(nir->stage));
      nir_print_shader(nir, stderr);
   }

   return nir;
}

nir_shader *
brw_nir_apply_sampler_key(nir_shader *nir,
                          const struct brw_device_info *devinfo,
                          const struct brw_sampler_prog_key_data *key_tex,
                          bool is_scalar)
{
   nir_lower_tex_options tex_options = { 0 };

   /* Iron Lake and prior require lowering of all rectangle textures */
   if (devinfo->gen < 6)
      tex_options.lower_rect = true;

   /* Prior to Broadwell, our hardware can't actually do GL_CLAMP */
   if (devinfo->gen < 8) {
      tex_options.saturate_s = key_tex->gl_clamp_mask[0];
      tex_options.saturate_t = key_tex->gl_clamp_mask[1];
      tex_options.saturate_r = key_tex->gl_clamp_mask[2];
   }

   /* Prior to Haswell, we have to fake texture swizzle */
   for (unsigned s = 0; s < MAX_SAMPLERS; s++) {
      if (key_tex->swizzles[s] == SWIZZLE_NOOP)
         continue;

      tex_options.swizzle_result |= (1 << s);
      for (unsigned c = 0; c < 4; c++)
         tex_options.swizzles[s][c] = GET_SWZ(key_tex->swizzles[s], c);
   }

   tex_options.lower_y_uv_external = key_tex->y_uv_image_mask;
   tex_options.lower_y_u_v_external = key_tex->y_u_v_image_mask;
   tex_options.lower_yx_xuxv_external = key_tex->yx_xuxv_image_mask;

   if (nir_lower_tex(nir, &tex_options)) {
      nir_validate_shader(nir);
      nir = nir_optimize(nir, is_scalar);
   }

   return nir;
}

enum brw_reg_type
brw_type_for_nir_type(nir_alu_type type)
{
   switch (type) {
   case nir_type_uint:
   case nir_type_uint32:
      return BRW_REGISTER_TYPE_UD;
   case nir_type_bool:
   case nir_type_int:
   case nir_type_bool32:
   case nir_type_int32:
      return BRW_REGISTER_TYPE_D;
   case nir_type_float:
   case nir_type_float32:
      return BRW_REGISTER_TYPE_F;
   case nir_type_float64:
      return BRW_REGISTER_TYPE_DF;
   case nir_type_int64:
   case nir_type_uint64:
      /* TODO we should only see these in moves, so for now it's ok, but when
       * we add actual 64-bit integer support we should fix this.
       */
      return BRW_REGISTER_TYPE_DF;
   default:
      unreachable("unknown type");
   }

   return BRW_REGISTER_TYPE_F;
}

/* Returns the glsl_base_type corresponding to a nir_alu_type.
 * This is used by both brw_vec4_nir and brw_fs_nir.
 */
enum glsl_base_type
brw_glsl_base_type_for_nir_type(nir_alu_type type)
{
   switch (type) {
   case nir_type_float:
   case nir_type_float32:
      return GLSL_TYPE_FLOAT;

   case nir_type_float64:
      return GLSL_TYPE_DOUBLE;

   case nir_type_int:
   case nir_type_int32:
      return GLSL_TYPE_INT;

   case nir_type_uint:
   case nir_type_uint32:
      return GLSL_TYPE_UINT;

   default:
      unreachable("bad type");
   }
}
