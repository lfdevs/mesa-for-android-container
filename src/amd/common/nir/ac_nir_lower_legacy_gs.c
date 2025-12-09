/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

typedef struct {
   ac_nir_legacy_gs_info *out_info;
   ac_nir_prerast_out out;

   nir_def *vertex_count[4];
   nir_def *primitive_count[4];
} lower_legacy_gs_state;

static bool
lower_legacy_gs_store_output(nir_builder *b, nir_intrinsic_instr *intrin,
                             lower_legacy_gs_state *s)
{
   ac_nir_gather_prerast_store_output_info(b, intrin, &s->out, true);
   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_emit_vertex_with_counter(nir_builder *b, nir_intrinsic_instr *intrin,
                                         lower_legacy_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);
   nir_def *vtxidx = intrin->src[0].ssa;

   nir_def *gsvs_ring = nir_load_ring_gsvs_amd(b, .stream_id = stream);
   nir_def *soffset = nir_load_ring_gs2vs_offset_amd(b);

   unsigned offset = 0;

   u_foreach_bit64_two_masks(slot, b->shader->info.outputs_written,
                             VARYING_SLOT_VAR0_16BIT, b->shader->info.outputs_written_16bit) {
      unsigned mask = ac_nir_gs_output_component_mask_with_stream(&s->out.infos[slot], stream);
      nir_def **output = s->out.outputs[slot];

      u_foreach_bit(c, mask) {
         /* The shader hasn't written this output yet. */
         if (!output[c] || ac_nir_is_const_output(&s->out, slot, c))
            continue;

         unsigned base = offset * b->shader->info.gs.vertices_out;
         nir_def *voffset = nir_ishl_imm(b, vtxidx, 2);

         /* extend 8/16 bit to 32 bit, 64 bit has been lowered */
         nir_def *store_val = nir_u2u32(b, output[c]);

         unsigned align_mul = 4;
         unsigned align_offset = 0;
         if (nir_src_is_const(intrin->src[0])) {
            unsigned v_const_offset = base + nir_src_as_uint(intrin->src[0]) * 4;
            align_mul = 16;
            align_offset = v_const_offset % align_mul;
         }

         nir_store_buffer_amd(b, store_val, gsvs_ring, voffset, soffset, nir_imm_int(b, 0),
                              .access = ACCESS_COHERENT | ACCESS_NON_TEMPORAL |
                                        ACCESS_IS_SWIZZLED_AMD,
                              .base = base,
                              /* For ACO to not reorder this store around EmitVertex/EndPrimitve */
                              .memory_modes = nir_var_shader_out,
                              .align_mul = align_mul, .align_offset = align_offset);
         offset += 4;
      }

      /* Clear all outputs (they are undefined after emit_vertex) */
      memset(s->out.outputs[slot], 0, sizeof(s->out.outputs[slot]));
   }

   assert(offset / 4 < 256);
   s->out_info->num_components_per_stream[stream] = offset / 4;

   /* Signal vertex emission. */
   nir_sendmsg_amd(b, nir_load_gs_wave_id_amd(b),
                   .base = AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8));

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_set_vertex_and_primitive_count(nir_builder *b, nir_intrinsic_instr *intrin,
                                               lower_legacy_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);

   s->vertex_count[stream] = intrin->src[0].ssa;
   s->primitive_count[stream] = intrin->src[1].ssa;

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_end_primitive_with_counter(nir_builder *b, nir_intrinsic_instr *intrin,
                                               lower_legacy_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);
   const unsigned stream = nir_intrinsic_stream_id(intrin);

   /* Signal primitive emission. */
   nir_sendmsg_amd(b, nir_load_gs_wave_id_amd(b),
                   .base = AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8));

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_legacy_gs_state *s = (lower_legacy_gs_state *) state;

   if (intrin->intrinsic == nir_intrinsic_store_output)
      return lower_legacy_gs_store_output(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_emit_vertex_with_counter)
      return lower_legacy_gs_emit_vertex_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_end_primitive_with_counter)
      return lower_legacy_gs_end_primitive_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_set_vertex_and_primitive_count)
      return lower_legacy_gs_set_vertex_and_primitive_count(b, intrin, s);

   return false;
}

static bool
gather_output_store_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_legacy_gs_state *s = (lower_legacy_gs_state *) state;

   if (intrin->intrinsic == nir_intrinsic_store_output) {
      ac_nir_gather_prerast_store_output_info(b, intrin, &s->out, false);
      return true;
   }

   return false;
}

static void
gather_output_stores(nir_shader *shader, lower_legacy_gs_state *s)
{
   nir_shader_intrinsics_pass(shader, gather_output_store_intrinsic, nir_metadata_none, s);
}

bool
ac_nir_lower_legacy_gs(nir_shader *nir, ac_nir_lower_legacy_gs_options *options,
                       nir_shader **gs_copy_shader, ac_nir_legacy_gs_info *out_info)
{
   lower_legacy_gs_state s = {out_info};

   gather_output_stores(nir, &s);
   ac_nir_compute_prerast_packed_output_info(&s.out);

   unsigned num_vertices_per_primitive = 0;
   switch (nir->info.gs.output_primitive) {
   case MESA_PRIM_POINTS:
      num_vertices_per_primitive = 1;
      break;
   case MESA_PRIM_LINE_STRIP:
      num_vertices_per_primitive = 2;
      break;
   case MESA_PRIM_TRIANGLE_STRIP:
      num_vertices_per_primitive = 3;
      break;
   default:
      UNREACHABLE("Invalid GS output primitive.");
      break;
   }

   nir_shader_intrinsics_pass(nir, lower_legacy_gs_intrinsic,
                              nir_metadata_control_flow, &s);

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder = nir_builder_at(nir_after_impl(impl));
   nir_builder *b = &builder;

   /* Emit shader query for mix use legacy/NGG GS */
   bool progress = ac_nir_gs_shader_query(b,
                                          options->has_gen_prim_query,
                                          options->has_pipeline_stats_query,
                                          options->has_pipeline_stats_query,
                                          num_vertices_per_primitive,
                                          64,
                                          s.vertex_count,
                                          s.primitive_count);

   /* Wait for all stores to finish. */
   nir_barrier(b, .execution_scope = SCOPE_INVOCATION,
                      .memory_scope = SCOPE_DEVICE,
                      .memory_semantics = NIR_MEMORY_RELEASE,
                      .memory_modes = nir_var_shader_out | nir_var_mem_ssbo |
                                      nir_var_mem_global | nir_var_image);

   /* Signal that the GS is done. */
   nir_sendmsg_amd(b, nir_load_gs_wave_id_amd(b),
                   .base = AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE);

   nir_progress(progress, impl, nir_metadata_none);

   *gs_copy_shader = ac_nir_create_gs_copy_shader(nir, options, &s.out);
   return true;
}
