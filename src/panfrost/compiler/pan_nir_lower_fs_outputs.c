/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

struct pan_nir_fs_outputs {
   bool has_pan_ops;
   nir_variable *depth;
   nir_variable *stencil;
   nir_variable *coverage;
   nir_variable *color[2];
   nir_variable *data[8][2];
};

static bool
gather_output_intrin(nir_builder *b, nir_intrinsic_instr *intrin, void *_data)
{
   struct pan_nir_fs_outputs *out = _data;

   if (intrin->intrinsic == nir_intrinsic_atest_pan ||
       intrin->intrinsic == nir_intrinsic_zs_emit_pan ||
       intrin->intrinsic == nir_intrinsic_blend_pan ||
       intrin->intrinsic == nir_intrinsic_blend2_pan)
      out->has_pan_ops = true;

   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_def *data = intrin->src[0].ssa;
   assert(nir_src_as_uint(intrin->src[1]) == 0);

   nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
   nir_alu_type src_type = nir_intrinsic_src_type(intrin);

   nir_variable **var;
   unsigned num_components = 1;
   const char *name;
   switch (io.location) {
   case FRAG_RESULT_DEPTH:
      var = &out->depth;
      name = "depth_tmp";
      break;
   case FRAG_RESULT_STENCIL:
      var = &out->stencil;
      name = "stencil_tmp";
      break;
   case FRAG_RESULT_SAMPLE_MASK:
      var = &out->coverage;
      name = "coverage_tmp";
      break;
   case FRAG_RESULT_COLOR:
      var = &out->color[io.dual_source_blend_index];
      num_components = 4;
      name = "color_tmp";
      break;
   default: {
      assert(io.location >= FRAG_RESULT_DATA0);
      assert(io.location <= FRAG_RESULT_DATA7);
      unsigned slot = io.location - FRAG_RESULT_DATA0;
      var = &out->data[slot][io.dual_source_blend_index];
      num_components = 4;
      name = "data_tmp";
      break;
   }
   }

   if (*var == NULL) {
      const struct glsl_type *type =
         glsl_vector_type(nir_get_glsl_base_type_for_nir_type(src_type),
                          num_components);
      *var = nir_local_variable_create(b->impl, type, name);
   }

   b->cursor = nir_after_instr(&intrin->instr);

   unsigned component = nir_intrinsic_component(intrin);
   data = nir_shift_channels(b, data, component, num_components);
   nir_component_mask_t write_mask =
      nir_intrinsic_write_mask(intrin) << component;

   nir_store_var(b, *var, data, write_mask);
   nir_instr_remove(&intrin->instr);

   return true;
}

bool
pan_nir_lower_fs_outputs(nir_shader *shader, bool skip_atest)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   struct pan_nir_fs_outputs out = {};
   bool has_store = nir_function_intrinsics_pass(impl, gather_output_intrin,
                                                 nir_metadata_control_flow,
                                                 &out);
   assert(!has_store || !out.has_pan_ops);
   if (!has_store && (out.has_pan_ops || skip_atest))
      return false;

   nir_builder builder = nir_builder_create(impl);
   nir_builder *b = &builder;

   /* We shouldn't have both gl_FragColor and gl_FragData */
   assert(!out.color[0] || !out.data[0][0]);
   nir_variable *color0 = out.color[0] ? out.color[0] : out.data[0][0];

   /* Default alpha to 1.0 for the sake of atest */
   b->cursor = nir_before_impl(impl);
   if (color0) {
      if (glsl_type_is_float(color0->type))
         nir_store_var(b, color0, nir_imm_vec4(b, 0, 0, 0, 1), 0b1000);
      else if (glsl_type_is_float_16(color0->type))
         nir_store_var(b, color0, nir_imm_vec4_16(b, 0, 0, 0, 1), 0b1000);
   }

   b->cursor = nir_after_impl(impl);

   nir_def *coverage;
   if (out.coverage) {
      /* For sample masks coming from the client, we have to AND with the
       * input sample mask in case they include disabled channels.
       */
      coverage = nir_iand(b, nir_load_var(b, out.coverage),
                             nir_load_sample_mask(b));
   } else {
      coverage = nir_load_cumulative_coverage_pan(b);
   }

   /* From the OpenGL 4.6 core specification:
    *
    *    "All alpha values in this section refer only to the alpha component
    *    of the fragment shader output linked to color number zero, index
    *    zero"
    */
   nir_def *alpha;
   if (color0 && glsl_type_is_float_16_32(color0->type))
      alpha = nir_channel(b, nir_load_var(b, color0), 3);
   else
      alpha = nir_imm_float(b, 1.0f);

   /* Emit ATEST if we have to, note ATEST requires a floating-point alpha
    * value, but render target #0 might not be floating point. However the
    * alpha value is only used for alpha-to-coverage, a stage which is
    * skipped for pure integer framebuffers, so the issue is moot.
    */
   if (!skip_atest)
      coverage = nir_atest_pan(b, coverage, alpha);

   /* We discard depth/stencil writes if early fragment tests is forced. */
   if ((out.depth || out.stencil) && !shader->info.fs.early_fragment_tests) {
      /* EMIT_ZS requires an ATEST */
      assert(!skip_atest);

      unsigned flags = 0;
      nir_def *depth, *stencil;
      if (out.depth) {
         depth = nir_load_var(b, out.depth);
         assert(depth->bit_size == 32);
         flags |= BITFIELD_BIT(FRAG_RESULT_DEPTH);
      } else {
         depth = nir_undef(b, 1, 32);
      }

      if (out.stencil) {
         stencil = nir_load_var(b, out.stencil);
         assert(stencil->bit_size == 32);
         flags |= BITFIELD_BIT(FRAG_RESULT_STENCIL);
      } else {
         stencil = nir_undef(b, 1, 32);
      }

      coverage = nir_zs_emit_pan(b, coverage, depth, stencil, .flags = flags);
   }

   for (unsigned i = 0; i < 8; i++) {
      nir_variable **color = out.color[0] ? out.color : out.data[i];
      if (!color[0])
         continue;

      nir_def *color0 = nir_load_var(b, color[0]);
      nir_alu_type color0_type =
         nir_get_nir_type_for_glsl_base_type(color[0]->type->base_type);

      nir_def *desc = nir_load_blend_descriptor_pan(b, .base = i);
      if (color[1]) {
         nir_def *color1 = nir_load_var(b, color[1]);
         nir_alu_type color1_type =
            nir_get_nir_type_for_glsl_base_type(color[1]->type->base_type);

         nir_blend2_pan(b, coverage, desc, color0, color1,
                        .src_type = color0_type, .dest_type = color1_type,
                        .io_semantics.location = FRAG_RESULT_DATA0 + i);
      } else {
         nir_blend_pan(b, coverage, desc, color0, .src_type = color0_type,
                       .io_semantics.location = FRAG_RESULT_DATA0 + i);
      }
   }

   return nir_progress(true, impl, nir_metadata_control_flow);
}
