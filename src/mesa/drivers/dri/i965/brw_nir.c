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
#include "compiler/nir/glsl_to_nir.h"
#include "compiler/nir/nir_builder.h"
#include "program/prog_to_nir.h"

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
struct add_const_offset_to_base_params {
   nir_builder b;
   nir_variable_mode mode;
};

static bool
add_const_offset_to_base_block(nir_block *block, void *closure)
{
   struct add_const_offset_to_base_params *params = closure;
   nir_builder *b = &params->b;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if ((params->mode == nir_var_shader_in && is_input(intrin)) ||
          (params->mode == nir_var_shader_out && is_output(intrin))) {
         nir_src *offset = nir_get_io_offset_src(intrin);
         nir_const_value *const_offset = nir_src_as_const_value(*offset);

         if (const_offset) {
            intrin->const_index[0] += const_offset->u[0];
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
   struct add_const_offset_to_base_params params = { .mode = mode };

   nir_foreach_function(nir, f) {
      if (f->impl) {
         nir_builder_init(&params.b, f->impl);
         nir_foreach_block(f->impl, add_const_offset_to_base_block, &params);
      }
   }
}

static bool
remap_vs_attrs(nir_block *block, void *closure)
{
   GLbitfield64 inputs_read = *((GLbitfield64 *) closure);

   nir_foreach_instr(block, instr) {
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
         int slot = _mesa_bitcount_64(inputs_read & BITFIELD64_MASK(attr));

         intrin->const_index[0] = 4 * slot;
      }
   }
   return true;
}

static bool
remap_inputs_with_vue_map(nir_block *block, void *closure)
{
   const struct brw_vue_map *vue_map = closure;

   nir_foreach_instr(block, instr) {
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

struct remap_patch_urb_offsets_state {
   nir_builder b;
   struct brw_vue_map vue_map;
};

static bool
remap_patch_urb_offsets(nir_block *block, void *closure)
{
   struct remap_patch_urb_offsets_state *state = closure;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      gl_shader_stage stage = state->b.shader->stage;

      if ((stage == MESA_SHADER_TESS_CTRL && is_output(intrin)) ||
          (stage == MESA_SHADER_TESS_EVAL && is_input(intrin))) {
         int vue_slot = state->vue_map.varying_to_slot[intrin->const_index[0]];
         assert(vue_slot != -1);
         intrin->const_index[0] = vue_slot;

         nir_src *vertex = nir_get_io_vertex_index_src(intrin);
         if (vertex) {
            nir_const_value *const_vertex = nir_src_as_const_value(*vertex);
            if (const_vertex) {
               intrin->const_index[0] += const_vertex->u[0] *
                                         state->vue_map.num_per_vertex_slots;
            } else {
               state->b.cursor = nir_before_instr(&intrin->instr);

               /* Multiply by the number of per-vertex slots. */
               nir_ssa_def *vertex_offset =
                  nir_imul(&state->b,
                           nir_ssa_for_src(&state->b, *vertex, 1),
                           nir_imm_int(&state->b,
                                       state->vue_map.num_per_vertex_slots));

               /* Add it to the existing offset */
               nir_src *offset = nir_get_io_offset_src(intrin);
               nir_ssa_def *total_offset =
                  nir_iadd(&state->b, vertex_offset,
                           nir_ssa_for_src(&state->b, *offset, 1));

               nir_instr_rewrite_src(&intrin->instr, offset,
                                     nir_src_for_ssa(total_offset));
            }
         }
      }
   }
   return true;
}

static void
brw_nir_lower_inputs(nir_shader *nir,
                     const struct brw_device_info *devinfo,
                     bool is_scalar,
                     bool use_legacy_snorm_formula,
                     const uint8_t *vs_attrib_wa_flags)
{
   switch (nir->stage) {
   case MESA_SHADER_VERTEX:
      /* Start with the location of the variable's base. */
      foreach_list_typed(nir_variable, var, node, &nir->inputs) {
         var->data.driver_location = var->data.location;
      }

      /* Now use nir_lower_io to walk dereference chains.  Attribute arrays
       * are loaded as one vec4 per element (or matrix column), so we use
       * type_size_vec4 here.
       */
      nir_lower_io(nir, nir_var_shader_in, type_size_vec4);

      /* This pass needs actual constants */
      nir_opt_constant_folding(nir);

      add_const_offset_to_base(nir, nir_var_shader_in);

      brw_nir_apply_attribute_workarounds(nir, use_legacy_snorm_formula,
                                          vs_attrib_wa_flags);

      if (is_scalar) {
         /* Finally, translate VERT_ATTRIB_* values into the actual registers.
          *
          * Note that we can use nir->info.inputs_read instead of
          * key->inputs_read since the two are identical aside from Gen4-5
          * edge flag differences.
          */
         GLbitfield64 inputs_read = nir->info.inputs_read;

         nir_foreach_function(nir, function) {
            if (function->impl) {
               nir_foreach_block(function->impl, remap_vs_attrs, &inputs_read);
            }
         }
      }
      break;
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_GEOMETRY: {
      if (!is_scalar && nir->stage == MESA_SHADER_GEOMETRY) {
         foreach_list_typed(nir_variable, var, node, &nir->inputs) {
            var->data.driver_location = var->data.location;
         }
      } else {
         /* The GLSL linker will have already matched up GS inputs and
          * the outputs of prior stages.  The driver does extend VS outputs
          * in some cases, but only for legacy OpenGL or Gen4-5 hardware,
          * neither of which offer geometry shader support.  So we can
          * safely ignore that.
          *
          * For SSO pipelines, we use a fixed VUE map layout based on variable
          * locations, so we can rely on rendezvous-by-location to make this
          * work.
          *
          * However, we need to ignore VARYING_SLOT_PRIMITIVE_ID, as it's not
          * written by previous stages and shows up via payload magic.
          */
         struct brw_vue_map input_vue_map;
         GLbitfield64 inputs_read =
            nir->info.inputs_read & ~VARYING_BIT_PRIMITIVE_ID;
         brw_compute_vue_map(devinfo, &input_vue_map, inputs_read,
                             nir->info.separate_shader ||
                             nir->stage == MESA_SHADER_TESS_CTRL);

         foreach_list_typed(nir_variable, var, node, &nir->inputs) {
            var->data.driver_location = var->data.location;
         }

         /* Inputs are stored in vec4 slots, so use type_size_vec4(). */
         nir_lower_io(nir, nir_var_shader_in, type_size_vec4);

         /* This pass needs actual constants */
         nir_opt_constant_folding(nir);

         add_const_offset_to_base(nir, nir_var_shader_in);

         nir_foreach_function(nir, function) {
            if (function->impl) {
               nir_foreach_block(function->impl, remap_inputs_with_vue_map,
                                 &input_vue_map);
            }
         }
      }
      break;
   }
   case MESA_SHADER_TESS_EVAL: {
      struct remap_patch_urb_offsets_state state;
      brw_compute_tess_vue_map(&state.vue_map,
                               nir->info.inputs_read & ~VARYING_BIT_PRIMITIVE_ID,
                               nir->info.patch_inputs_read);

      foreach_list_typed(nir_variable, var, node, &nir->inputs) {
         var->data.driver_location = var->data.location;
      }

      nir_lower_io(nir, nir_var_shader_in, type_size_vec4);

      /* This pass needs actual constants */
      nir_opt_constant_folding(nir);

      add_const_offset_to_base(nir, nir_var_shader_in);

      nir_foreach_function(nir, function) {
         if (function->impl) {
            nir_builder_init(&state.b, function->impl);
            nir_foreach_block(function->impl, remap_patch_urb_offsets, &state);
         }
      }
      break;
   }
   case MESA_SHADER_FRAGMENT:
      assert(is_scalar);
      nir_assign_var_locations(&nir->inputs, &nir->num_inputs,
                               type_size_scalar);
      break;
   case MESA_SHADER_COMPUTE:
      /* Compute shaders have no inputs. */
      assert(exec_list_is_empty(&nir->inputs));
      break;
   default:
      unreachable("unsupported shader stage");
   }
}

static void
brw_nir_lower_outputs(nir_shader *nir,
                      const struct brw_device_info *devinfo,
                      bool is_scalar)
{
   switch (nir->stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
      if (is_scalar) {
         nir_assign_var_locations(&nir->outputs, &nir->num_outputs,
                                  type_size_vec4_times_4);
         nir_lower_io(nir, nir_var_shader_out, type_size_vec4_times_4);
      } else {
         nir_foreach_variable(var, &nir->outputs)
            var->data.driver_location = var->data.location;
      }
      break;
   case MESA_SHADER_TESS_CTRL: {
      struct remap_patch_urb_offsets_state state;
      brw_compute_tess_vue_map(&state.vue_map, nir->info.outputs_written,
                               nir->info.patch_outputs_written);

      nir_foreach_variable(var, &nir->outputs) {
         var->data.driver_location = var->data.location;
      }

      nir_lower_io(nir, nir_var_shader_out, type_size_vec4);

      /* This pass needs actual constants */
      nir_opt_constant_folding(nir);

      add_const_offset_to_base(nir, nir_var_shader_out);

      nir_foreach_function(nir, function) {
         if (function->impl) {
            nir_builder_init(&state.b, function->impl);
            nir_foreach_block(function->impl, remap_patch_urb_offsets, &state);
         }
      }
      break;
   }
   case MESA_SHADER_FRAGMENT:
      nir_assign_var_locations(&nir->outputs, &nir->num_outputs,
                               type_size_scalar);
      break;
   case MESA_SHADER_COMPUTE:
      /* Compute shaders have no outputs. */
      assert(exec_list_is_empty(&nir->outputs));
      break;
   default:
      unreachable("unsupported shader stage");
   }
}

static int
type_size_scalar_bytes(const struct glsl_type *type)
{
   return type_size_scalar(type) * 4;
}

static int
type_size_vec4_bytes(const struct glsl_type *type)
{
   return type_size_vec4(type) * 16;
}

static void
brw_nir_lower_uniforms(nir_shader *nir, bool is_scalar)
{
   if (is_scalar) {
      nir_assign_var_locations(&nir->uniforms, &nir->num_uniforms,
                               type_size_scalar_bytes);
      nir_lower_io(nir, nir_var_uniform, type_size_scalar_bytes);
   } else {
      nir_assign_var_locations(&nir->uniforms, &nir->num_uniforms,
                               type_size_vec4_bytes);
      nir_lower_io(nir, nir_var_uniform, type_size_vec4_bytes);
   }
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
brw_preprocess_nir(nir_shader *nir, bool is_scalar)
{
   bool progress; /* Written by OPT and OPT_V */
   (void)progress;

   if (nir->stage == MESA_SHADER_GEOMETRY)
      OPT(nir_lower_gs_intrinsics);

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

   OPT(nir_remove_dead_variables);

   return nir;
}

/** Lower input and output loads and stores for i965. */
nir_shader *
brw_nir_lower_io(nir_shader *nir,
                 const struct brw_device_info *devinfo,
                 bool is_scalar,
                 bool use_legacy_snorm_formula,
                 const uint8_t *vs_attrib_wa_flags)
{
   bool progress; /* Written by OPT and OPT_V */
   (void)progress;

   OPT_V(brw_nir_lower_inputs, devinfo, is_scalar,
         use_legacy_snorm_formula, vs_attrib_wa_flags);
   OPT_V(brw_nir_lower_outputs, devinfo, is_scalar);
   OPT_V(nir_lower_io, nir_var_all, is_scalar ? type_size_scalar : type_size_vec4);

   return nir_optimize(nir, is_scalar);
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
      nir_foreach_function(nir, function) {
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
brw_create_nir(struct brw_context *brw,
               const struct gl_shader_program *shader_prog,
               const struct gl_program *prog,
               gl_shader_stage stage,
               bool is_scalar)
{
   struct gl_context *ctx = &brw->ctx;
   const struct brw_device_info *devinfo = brw->intelScreen->devinfo;
   const nir_shader_compiler_options *options =
      ctx->Const.ShaderCompilerOptions[stage].NirOptions;
   bool progress;
   nir_shader *nir;

   /* First, lower the GLSL IR or Mesa IR to NIR */
   if (shader_prog) {
      nir = glsl_to_nir(shader_prog, stage, options);
   } else {
      nir = prog_to_nir(prog, options);
      OPT_V(nir_convert_to_ssa); /* turn registers into SSA */
   }
   nir_validate_shader(nir);

   (void)progress;

   nir = brw_preprocess_nir(nir, is_scalar);

   OPT(nir_lower_system_values);
   OPT_V(brw_nir_lower_uniforms, is_scalar);

   if (shader_prog) {
      OPT_V(nir_lower_samplers, shader_prog);
      OPT_V(nir_lower_atomics, shader_prog);
   }

   if (nir->stage != MESA_SHADER_VERTEX &&
       nir->stage != MESA_SHADER_TESS_CTRL &&
       nir->stage != MESA_SHADER_TESS_EVAL) {
      nir = brw_nir_lower_io(nir, devinfo, is_scalar, false, NULL);
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
      return BRW_REGISTER_TYPE_UD;
   case nir_type_bool:
   case nir_type_int:
      return BRW_REGISTER_TYPE_D;
   case nir_type_float:
      return BRW_REGISTER_TYPE_F;
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
      return GLSL_TYPE_FLOAT;

   case nir_type_int:
      return GLSL_TYPE_INT;

   case nir_type_uint:
      return GLSL_TYPE_UINT;

   default:
      unreachable("bad type");
   }
}
