/* Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "si_shader_internal.h"
#include "nir.h"
#include "nir_range_analysis.h"
#include "sid.h"

void si_get_shader_variant_info(struct si_shader *shader,
                                struct si_temp_shader_variant_info *temp_info, nir_shader *nir)
{
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   assert(nir->info.use_aco_amd == si_shader_uses_aco(shader));
   const BITSET_WORD *sysvals = nir->info.system_values_read;
   /* Find out which frag coord components are used. */
   uint8_t frag_coord_mask = 0;

   nir_divergence_analysis(nir);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Since flat+convergent and non-flat components can occur in the same vec4, start with
       * all PS inputs as flat and change them to smooth when we find a component that's
       * interpolated.
       */
      for (unsigned i = 0; i < ARRAY_SIZE(shader->info.ps_inputs); i++)
         shader->info.ps_inputs[i].interpolate = INTERP_MODE_FLAT;

      shader->info.num_ps_per_primitive_inputs =
         util_bitcount64(nir->info.per_primitive_inputs);

      /* gl_Layer is always from shader arg, not varying */
      uint64_t maybe_per_prim_inputs =
         nir->info.inputs_read & ~nir->info.per_primitive_inputs &
         (VARYING_BIT_PRIMITIVE_ID | VARYING_BIT_VIEWPORT);
      shader->info.num_ps_maybe_per_primitive_inputs =
         util_bitcount64(maybe_per_prim_inputs);
   }

   nir_foreach_block(block, nir_shader_get_entrypoint(nir)) {
      nir_foreach_instr(instr, block) {
         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            switch (intr->intrinsic) {
            case nir_intrinsic_load_instance_id:
               shader->info.uses_instance_id = true;
               break;
            case nir_intrinsic_load_base_instance:
               shader->info.uses_base_instance = true;
               break;
            case nir_intrinsic_load_draw_id:
               shader->info.uses_draw_id = true;
               break;
            case nir_intrinsic_load_frag_coord:
            case nir_intrinsic_load_sample_pos:
               frag_coord_mask |= nir_def_components_read(&intr->def);
               break;
            case nir_intrinsic_load_input:
            case nir_intrinsic_load_input_vertex:
            case nir_intrinsic_load_per_vertex_input:
            case nir_intrinsic_load_per_primitive_input:
            case nir_intrinsic_load_interpolated_input: {
               if (nir->info.stage == MESA_SHADER_VERTEX) {
                  shader->info.uses_vmem_load_other = true;

                  if (intr->intrinsic == nir_intrinsic_load_input &&
                      (shader->key.ge.mono.instance_divisor_is_one |
                       shader->key.ge.mono.instance_divisor_is_fetched) &
                      BITFIELD_BIT(nir_intrinsic_base(intr))) {
                     /* Instanced attribs. */
                     shader->info.uses_instance_id = true;
                     shader->info.uses_base_instance = true;
                  }
               } else if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
                  shader->info.uses_vmem_load_other = true;
               } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
                  nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
                  unsigned index = nir_intrinsic_base(intr);
                  assert(sem.num_slots == 1);

                  shader->info.num_ps_inputs = MAX2(shader->info.num_ps_inputs, index + 1);
                  shader->info.ps_inputs[index].semantic = sem.location;
                  /* Determine interpolation mode. This only cares about FLAT/SMOOTH/COLOR.
                   * COLOR is only for nir_intrinsic_load_color0/1.
                   */
                  if (intr->intrinsic == nir_intrinsic_load_interpolated_input) {
                     shader->info.ps_inputs[index].interpolate = INTERP_MODE_SMOOTH;
                     if (intr->def.bit_size == 16)
                        shader->info.ps_inputs[index].fp16_lo_hi_valid |= 0x1 << sem.high_16bits;
                  } else if (intr->intrinsic == nir_intrinsic_load_per_primitive_input) {
                     /* per primitive input from mesh shader */
                     shader->info.ps_inputs[index].interpolate = INTERP_MODE_NONE;
                  }
               }
               break;
            }
            case nir_intrinsic_load_color0:
               assert(!shader->is_monolithic);
               shader->info.ps_colors_read |= nir_def_components_read(&intr->def);
               break;
            case nir_intrinsic_load_color1:
               assert(!shader->is_monolithic);
               shader->info.ps_colors_read |= nir_def_components_read(&intr->def) << 4;
               break;
            case nir_intrinsic_load_ubo:
               if (intr->src[1].ssa->divergent)
                  shader->info.uses_vmem_load_other = true;
               break;
            case nir_intrinsic_load_constant:
               if (intr->src[0].ssa->divergent)
                  shader->info.uses_vmem_load_other = true;
               break;
            /* Global */
            case nir_intrinsic_load_global:
            case nir_intrinsic_global_atomic:
            case nir_intrinsic_global_atomic_swap:
            /* SSBOs (this list is from si_nir_lower_resource.c) */
            case nir_intrinsic_load_ssbo:
            case nir_intrinsic_ssbo_atomic:
            case nir_intrinsic_ssbo_atomic_swap:
            /* Images (this list is from si_nir_lower_resource.c) */
            case nir_intrinsic_image_deref_load:
            case nir_intrinsic_image_deref_sparse_load:
            case nir_intrinsic_image_deref_fragment_mask_load_amd:
            case nir_intrinsic_image_deref_atomic:
            case nir_intrinsic_image_deref_atomic_swap:
            case nir_intrinsic_bindless_image_load:
            case nir_intrinsic_bindless_image_sparse_load:
            case nir_intrinsic_bindless_image_fragment_mask_load_amd:
            case nir_intrinsic_bindless_image_atomic:
            case nir_intrinsic_bindless_image_atomic_swap:
            /* Scratch */
            case nir_intrinsic_load_scratch:
            /* AMD-specific. */
            case nir_intrinsic_load_buffer_amd:
               /* Atomics without return are not treated as loads. */
               if (nir_def_components_read(&intr->def) &&
                   (!nir_intrinsic_has_atomic_op(intr) ||
                    nir_intrinsic_atomic_op(intr) != nir_atomic_op_ordered_add_gfx12_amd))
                  shader->info.uses_vmem_load_other = true;
               break;
            case nir_intrinsic_store_output:
            case nir_intrinsic_store_per_vertex_output:
               if (nir->info.stage == MESA_SHADER_FRAGMENT) {
                  nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

                  if (sem.location == FRAG_RESULT_DEPTH)
                     shader->info.writes_z = true;
                  else if (sem.location == FRAG_RESULT_STENCIL)
                     shader->info.writes_stencil = true;
                  else if (sem.location == FRAG_RESULT_SAMPLE_MASK)
                     shader->info.writes_sample_mask = true;
               } else if ((nir->info.stage <= MESA_SHADER_GEOMETRY &&
                           !shader->key.ge.as_ls && !shader->key.ge.as_es) ||
                          nir->info.stage == MESA_SHADER_MESH) {
                  nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

                  /* Clip/cull distances must be gathered manually because nir_opt_clip_cull_const
                   * can reduce their number.
                   */
                  if ((sem.location == VARYING_SLOT_CLIP_DIST0 ||
                       sem.location == VARYING_SLOT_CLIP_DIST1) && !sem.no_sysval_output) {
                     assert(nir_src_as_uint(*nir_get_io_offset_src(intr)) == 0);
                     unsigned index = (sem.location - VARYING_SLOT_CLIP_DIST0) * 4 +
                                      nir_intrinsic_component(intr);

                     if (index < nir->info.clip_distance_array_size) {
                        assert(shader->selector->info.clipdist_mask & BITFIELD_BIT(index));
                        shader->info.clipdist_mask |= BITFIELD_BIT(index);
                     } else if (index < nir->info.clip_distance_array_size +
                                        nir->info.cull_distance_array_size) {
                        shader->info.culldist_mask |= BITFIELD_BIT(index);
                     }
                  }
               }
               break;
            case nir_intrinsic_demote:
            case nir_intrinsic_demote_if:
            case nir_intrinsic_terminate:
            case nir_intrinsic_terminate_if:
               if (nir->info.stage == MESA_SHADER_FRAGMENT)
                  shader->info.uses_discard = true;
               break;
            default:
               break;
            }
            break;
         }

         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);

            temp_info->has_non_uniform_tex_access |= tex->texture_non_uniform || tex->sampler_non_uniform;
            temp_info->has_shadow_comparison |= tex->is_shadow;

            /* Gather the types of used VMEM instructions that return something. */
            switch (tex->op) {
            case nir_texop_tex:
            case nir_texop_txb:
            case nir_texop_txl:
            case nir_texop_txd:
            case nir_texop_lod:
            case nir_texop_tg4:
               shader->info.uses_vmem_sampler_or_bvh = true;
               break;
            case nir_texop_txs:
            case nir_texop_query_levels:
            case nir_texop_texture_samples:
            case nir_texop_descriptor_amd:
            case nir_texop_sampler_descriptor_amd:
               /* These just return the descriptor or information from it. */
               break;
            default:
               shader->info.uses_vmem_load_other = true;
               break;
            }
            break;
         }

         default:
            break;
         }
      }
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Add both front and back color inputs. */
      if (!shader->is_monolithic) {
         unsigned index = shader->info.num_ps_inputs;

         for (unsigned back = 0; back < 2; back++) {
            for (unsigned i = 0; i < 2; i++) {
               if ((shader->info.ps_colors_read >> (i * 4)) & 0xf) {
                  assert(index < ARRAY_SIZE(shader->info.ps_inputs));
                  shader->info.ps_inputs[index].semantic =
                     (back ? VARYING_SLOT_BFC0 : VARYING_SLOT_COL0) + i;

                  enum glsl_interp_mode mode = i ? nir->info.fs.color1_interp
                                                 : nir->info.fs.color0_interp;
                  shader->info.ps_inputs[index].interpolate =
                     mode == INTERP_MODE_NONE ? INTERP_MODE_COLOR : mode;
                  index++;

                  /* Back-face colors don't increment num_ps_inputs. si_emit_spi_map will use
                   * back-face colors conditionally only when needed.
                   */
                  if (!back)
                     shader->info.num_ps_inputs++;
               }
            }
         }
      }

      /* ACO needs spi_ps_input_ena before si_init_shader_args. */
      shader->config.spi_ps_input_ena =
         S_0286CC_PERSP_SAMPLE_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_BARYCENTRIC_PERSP_SAMPLE)) |
         S_0286CC_PERSP_CENTER_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL)) |
         S_0286CC_PERSP_CENTROID_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_BARYCENTRIC_PERSP_CENTROID)) |
         S_0286CC_LINEAR_SAMPLE_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_BARYCENTRIC_LINEAR_SAMPLE)) |
         S_0286CC_LINEAR_CENTER_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_BARYCENTRIC_LINEAR_PIXEL)) |
         S_0286CC_LINEAR_CENTROID_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_BARYCENTRIC_LINEAR_CENTROID)) |
         S_0286CC_POS_X_FLOAT_ENA(!!(frag_coord_mask & 0x1)) |
         S_0286CC_POS_Y_FLOAT_ENA(!!(frag_coord_mask & 0x2)) |
         S_0286CC_POS_Z_FLOAT_ENA(!!(frag_coord_mask & 0x4)) |
         S_0286CC_POS_W_FLOAT_ENA(!!(frag_coord_mask & 0x8)) |
         S_0286CC_FRONT_FACE_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_FRONT_FACE) |
                                 BITSET_TEST(sysvals, SYSTEM_VALUE_FRONT_FACE_FSIGN)) |
         S_0286CC_ANCILLARY_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_SAMPLE_ID) |
                                BITSET_TEST(sysvals, SYSTEM_VALUE_LAYER_ID)) |
         S_0286CC_SAMPLE_COVERAGE_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_SAMPLE_MASK_IN)) |
         S_0286CC_POS_FIXED_PT_ENA(BITSET_TEST(sysvals, SYSTEM_VALUE_PIXEL_COORD));

      if (shader->is_monolithic) {
         si_fixup_spi_ps_input_config(shader);
         shader->config.spi_ps_input_addr = shader->config.spi_ps_input_ena;
      } else {
         /* Part mode will call si_fixup_spi_ps_input_config() when combining multi
          * shader part in si_shader_select_ps_parts().
          *
          * Reserve register locations for VGPR inputs the PS prolog may need.
          */
         shader->config.spi_ps_input_addr = shader->config.spi_ps_input_ena |
                                            SI_SPI_PS_INPUT_ADDR_FOR_PROLOG;
      }
   }

   if ((nir->info.stage <= MESA_SHADER_GEOMETRY &&
        !shader->key.ge.as_ls && !shader->key.ge.as_es) ||
       nir->info.stage == MESA_SHADER_MESH) {
      if (nir->xfb_info) {
         unsigned num_streamout_dwords = 0;

         for (unsigned i = 0; i < 4; i++)
            num_streamout_dwords += nir->info.xfb_stride[i];
         shader->info.num_streamout_vec4s = DIV_ROUND_UP(num_streamout_dwords, 4);
      }

      if (shader->key.ge.mono.write_pos_to_clipvertex ||
          nir->info.outputs_written & VARYING_BIT_CLIP_VERTEX)
         shader->info.clipdist_mask = SI_USER_CLIP_PLANE_MASK;

      shader->info.clipdist_mask &= ~shader->key.ge.opt.kill_clip_distances;
   }
}

/* Late shader variant info for AMD-specific intrinsics. */
void si_get_late_shader_variant_info(struct si_shader *shader, struct si_shader_args *args,
                                     nir_shader *nir)
{
   /* mesh shader know this after ac_nir_lower_ngg_mesh() */
   shader->info.shared_size = nir->info.shared_size;

   nir_foreach_block(block, nir_shader_get_entrypoint(nir)) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         switch (intr->intrinsic) {
         case nir_intrinsic_load_scalar_arg_amd:
            if (!args->vs_state_bits.used ||
                nir_intrinsic_base(intr) != args->vs_state_bits.arg_index)
               continue;

            assert(args->vs_state_bits.used);

            /* Gather which VS_STATE and GS_STATE user SGPR bits are used. */
            uint32_t bits_used = nir_def_bits_used(nir_instr_def(instr));

            if (nir->info.stage == MESA_SHADER_VERTEX &&
                bits_used & ENCODE_FIELD(VS_STATE_INDEXED, ~0))
               shader->info.uses_vs_state_indexed = true;

            if (!shader->key.ge.as_es && shader->key.ge.as_ngg) {
               if (bits_used & ENCODE_FIELD(GS_STATE_PROVOKING_VTX_FIRST, ~0))
                  shader->info.uses_gs_state_provoking_vtx_first = true;

               if (bits_used & ENCODE_FIELD(GS_STATE_OUTPRIM, ~0))
                  shader->info.uses_gs_state_outprim = true;
            }
            break;
         case nir_intrinsic_export_amd: {
            unsigned target = nir_intrinsic_base(intr);
            if (target >= V_008DFC_SQ_EXP_POS && target <= V_008DFC_SQ_EXP_POS + 4) {
               shader->info.nr_pos_exports = MAX2(shader->info.nr_pos_exports,
                                                  target - V_008DFC_SQ_EXP_POS + 1);
            }
            break;
         }
         default:
            break;
         }
      }
   }
}

void si_set_spi_ps_input_config_for_separate_prolog(struct si_shader *shader)
{
   const union si_shader_key *key = &shader->key;

   /* Enable POS_FIXED_PT if polygon stippling is enabled. */
   if (key->ps.part.prolog.poly_stipple)
      shader->config.spi_ps_input_ena |= S_0286CC_POS_FIXED_PT_ENA(1);

   /* Set up the enable bits for per-sample shading if needed. */
   if (key->ps.part.prolog.force_persp_sample_interp &&
       (G_0286CC_PERSP_CENTER_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTER_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
   }
   if (key->ps.part.prolog.force_linear_sample_interp &&
       (G_0286CC_LINEAR_CENTER_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTER_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_SAMPLE_ENA(1);
   }
   if (key->ps.part.prolog.force_persp_center_interp &&
       (G_0286CC_PERSP_SAMPLE_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_PERSP_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_SAMPLE_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_PERSP_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_CENTER_ENA(1);
   }
   if (key->ps.part.prolog.force_linear_center_interp &&
       (G_0286CC_LINEAR_SAMPLE_ENA(shader->config.spi_ps_input_ena) ||
        G_0286CC_LINEAR_CENTROID_ENA(shader->config.spi_ps_input_ena))) {
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_SAMPLE_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_LINEAR_CENTROID_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_LINEAR_CENTER_ENA(1);
   }

   /* The sample mask fixup requires the sample ID. */
   if (key->ps.part.prolog.samplemask_log_ps_iter)
      shader->config.spi_ps_input_ena |= S_0286CC_ANCILLARY_ENA(1);

   if (key->ps.part.prolog.force_samplemask_to_helper_invocation) {
      assert(key->ps.part.prolog.samplemask_log_ps_iter == 0);
      assert(!key->ps.mono.poly_line_smoothing);
      shader->config.spi_ps_input_ena &= C_0286CC_SAMPLE_COVERAGE_ENA;
   }

   /* The sample mask fixup has an optimization that replaces the sample mask with the sample ID. */
   if (key->ps.part.prolog.samplemask_log_ps_iter == 3)
      shader->config.spi_ps_input_ena &= C_0286CC_SAMPLE_COVERAGE_ENA;

   if (key->ps.part.prolog.get_frag_coord_from_pixel_coord) {
      shader->config.spi_ps_input_ena &= C_0286CC_POS_X_FLOAT_ENA;
      shader->config.spi_ps_input_ena &= C_0286CC_POS_Y_FLOAT_ENA;
      shader->config.spi_ps_input_ena |= S_0286CC_POS_FIXED_PT_ENA(1);
   }
}

void si_fixup_spi_ps_input_config(struct si_shader *shader)
{
   /* POW_W_FLOAT requires that one of the perspective weights is enabled. */
   if (G_0286CC_POS_W_FLOAT_ENA(shader->config.spi_ps_input_ena) &&
       !(shader->config.spi_ps_input_ena & 0xf)) {
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
   }

   /* At least one pair of interpolation weights must be enabled. */
   if (!(shader->config.spi_ps_input_ena & 0x7f))
      shader->config.spi_ps_input_ena |= S_0286CC_PERSP_SAMPLE_ENA(1);
}
