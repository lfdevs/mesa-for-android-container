/*
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"
#include "amdgfxregs.h"

#include "nir_builder.h"
#include "nir_xfb_info.h"

void
ac_nir_store_var_components(nir_builder *b, nir_variable *var, nir_def *value,
                            unsigned component, unsigned writemask)
{
   /* component store */
   if (value->num_components != 4) {
      nir_def *undef = nir_undef(b, 1, value->bit_size);

      /* add undef component before and after value to form a vec4 */
      nir_def *comp[4];
      for (int i = 0; i < 4; i++) {
         comp[i] = (i >= component && i < component + value->num_components) ?
            nir_channel(b, value, i - component) : undef;
      }

      value = nir_vec(b, comp, 4);
      writemask <<= component;
   } else {
      /* if num_component==4, there should be no component offset */
      assert(component == 0);
   }

   nir_store_var(b, var, value, writemask);
}

unsigned
ac_nir_map_io_location(unsigned location,
                       uint64_t mask,
                       ac_nir_map_io_driver_location map_io)
{
   /* Unlinked shaders:
    * We are unaware of the inputs of the next stage while lowering outputs.
    * The driver needs to pass a callback to map varyings to a fixed location.
    */
   if (map_io)
      return map_io(location);

   /* Linked shaders:
    * Take advantage of knowledge of the inputs of the next stage when lowering outputs.
    * Map varyings to a prefix sum of the IO mask to save space in LDS or VRAM.
    */
   assert(mask & BITFIELD64_BIT(location));
   return util_bitcount64(mask & BITFIELD64_MASK(location));
}

/**
 * This function calculates the full offset of an input/output.
 */
nir_def *
ac_nir_calc_io_off(nir_builder *b, unsigned component, nir_def *io_offset, nir_def *base_stride,
                   unsigned component_stride, unsigned mapped_driver_location)
{
   /* base is the driver_location, which is in slots (1 slot = 4x4 bytes) */
   nir_def *base_op = nir_imul_imm(b, base_stride, mapped_driver_location);

   /* offset should be interpreted in relation to the base,
    * so the instruction effectively reads/writes another input/output
    * when it has an offset
    */
   nir_def *offset_op = nir_imul(b, base_stride, io_offset);

   /* component is in bytes */
   unsigned const_op = component * component_stride;

   return nir_iadd_imm_nuw(b, nir_iadd_nuw(b, base_op, offset_op), const_op);
}

/* Process the given store_output intrinsic and process its information.
 * Meant to be used for VS/TES/GS when they are the last pre-rasterization stage.
 *
 * Assumptions:
 * - We called nir_lower_io_vars_to_temporaries on the shader
 * - 64-bit outputs are lowered
 * - no indirect indexing is present
 */
void ac_nir_gather_prerast_store_output_info(nir_builder *b, nir_intrinsic_instr *intrin,
                                             ac_nir_prerast_out *out, bool gather_values)
{
   assert(intrin->intrinsic == nir_intrinsic_store_output);
   assert(nir_src_is_const(intrin->src[1]) && !nir_src_as_uint(intrin->src[1]));

   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   const unsigned slot = io_sem.location;

   nir_def *store_val = intrin->src[0].ssa;
   assert(store_val->bit_size == 16 || store_val->bit_size == 32);

   nir_def **output = out->outputs[slot];
   nir_alu_type *type = out->types[slot];
   ac_nir_prerast_per_output_info *info = &out->infos[slot];

   unsigned component_offset = nir_intrinsic_component(intrin);
   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   nir_alu_type src_type = nir_intrinsic_src_type(intrin);
   assert(nir_alu_type_get_type_size(src_type) == store_val->bit_size);

   b->cursor = nir_before_instr(&intrin->instr);

   u_foreach_bit (i, write_mask) {
      const unsigned stream = (io_sem.gs_streams >> (i * 2)) & 0x3;

      if (b->shader->info.stage == MESA_SHADER_GEOMETRY) {
         if (!(b->shader->info.gs.active_stream_mask & (1 << stream)))
            continue;
      }

      const unsigned c = component_offset + i;

      /* The same output component should always belong to the same stream. */
      assert(!(info->components_mask & (1 << c)) ||
             ((info->stream >> (c * 2)) & 3) == stream);

      /* Components of the same output slot may belong to different streams. */
      info->stream |= stream << (c * 2);
      info->components_mask |= BITFIELD_BIT(c);
      if (io_sem.clamp) {
         info->clamp_components_mask |= BITFIELD_BIT(c);
      }

      if (!io_sem.no_varying)
         info->as_varying_mask |= BITFIELD_BIT(c);
      if (!io_sem.no_sysval_output)
         info->as_sysval_mask |= BITFIELD_BIT(c);

      nir_def *store_component = nir_channel(b, intrin->src[0].ssa, i);

      /* Gather constant output components. This must be done only once when we gather info but not SSA defs
       * because we want to gather all stores and then determine if they are all constants and if they are
       * the same constants.
       */
      if (!gather_values && !(info->nonconst_mask & BITFIELD_BIT(c))) {
         nir_scalar s = nir_scalar_resolved(store_component, 0);

         if (nir_scalar_is_const(s)) {
            nir_const_value value = nir_scalar_as_const_value(s);
            uint32_t *saved_value, new_value;

            /* Get the old value pointer and the new value. */
            if (store_component->bit_size == 16) {
               saved_value = &out->const_values[slot][c];

               if (io_sem.high_16bits)
                  new_value = (*saved_value & 0xffff) | ((uint32_t)value.u16 << 16);
               else
                  new_value = (*saved_value & 0xffff0000) | value.u16;
            } else {
               saved_value = &out->const_values[slot][c];
               new_value = value.u32;
            }

            /* Update constant info. */
            if (!(info->const_mask & BITFIELD_BIT(c))) {
               *saved_value = new_value;
               info->const_mask |= BITFIELD_BIT(c);
            } else if (*saved_value != new_value) {
               /* Different stores write different constants. */
               info->const_mask &= ~BITFIELD_BIT(c);
               info->nonconst_mask |= BITFIELD_BIT(c);
            }
         } else {
            info->const_mask &= ~BITFIELD_BIT(c);
            info->nonconst_mask |= BITFIELD_BIT(c);
         }
      }

      if (store_val->bit_size == 16) {
         if (gather_values) {
            if (io_sem.high_16bits) {
               nir_def *lo = output[c] ? nir_unpack_32_2x16_split_x(b, output[c]) : nir_imm_intN_t(b, 0, 16);
               output[c] = nir_pack_32_2x16_split(b, lo, store_component);
            } else {
               nir_def *hi = output[c] ? nir_unpack_32_2x16_split_y(b, output[c]) : nir_imm_intN_t(b, 0, 16);
               output[c] = nir_pack_32_2x16_split(b, store_component, hi);
            }
         }
         type[c] = nir_type_uint32;
      } else {
         if (gather_values)
            output[c] = store_component;
         type[c] = src_type;
      }
   }

   /* GS stores all outputs in LDS, while VS/TES only store XFB outputs in LDS. */
   if (b->shader->info.stage == MESA_SHADER_GEOMETRY) {
      info->xfb_lds_components_mask |= write_mask << component_offset;
   } else {
      info->xfb_lds_components_mask |= nir_instr_xfb_write_mask(intrin) & (write_mask << component_offset);

      /* For VS, we store edge flags in LDS where the LDS space is shared with XFB, so we need
       * to include edge flags in the XFB LDS size even though XFB doesn't use it.
       * Only the prim export uses it.
       */
      if (b->shader->info.stage == MESA_SHADER_VERTEX && slot == VARYING_SLOT_EDGE) {
         assert(write_mask == 0x1);
         info->xfb_lds_components_mask |= write_mask;
      }
   }
}

static nir_intrinsic_instr *
export(nir_builder *b, nir_def *val, nir_def *row, unsigned target, unsigned flags,
       unsigned write_mask)
{
   if (row) {
      return nir_export_row_amd(b, val, row, .target = target, .flags = flags,
                                .enabled_channels = write_mask);
   } else {
      return nir_export_amd(b, val, .target = target, .flags = flags,
                            .enabled_channels = write_mask);
   }
}

void
ac_nir_export_primitive(nir_builder *b, nir_def *prim, nir_def *row)
{
   unsigned write_mask = BITFIELD_MASK(prim->num_components);

   export(b, nir_pad_vec4(b, prim), row, V_008DFC_SQ_EXP_PRIM, AC_EXP_FLAG_DONE,
          write_mask);
}

static nir_def *
get_export_output(nir_builder *b, nir_def **output)
{
   nir_def *vec[4];
   for (int i = 0; i < 4; i++) {
      if (output[i])
         vec[i] = nir_u2uN(b, output[i], 32);
      else
         vec[i] = nir_undef(b, 1, 32);
   }

   return nir_vec(b, vec, 4);
}

static nir_def *
get_pos0_output(nir_builder *b, nir_def **output)
{
   /* Some applications don't write position but expect (0, 0, 0, 1)
    * so use that value instead of undef when it isn't written.
    */
   nir_def *vec[4] = {0};

   for (int i = 0; i < 4; i++) {
      if (output[i])
         vec[i] = nir_u2u32(b, output[i]);
     else
         vec[i] = nir_imm_float(b, i == 3 ? 1.0 : 0.0);
   }

   return nir_vec(b, vec, 4);
}

void
ac_nir_export_position(nir_builder *b,
                       enum amd_gfx_level gfx_level,
                       uint32_t export_clipdist_mask,
                       bool dont_export_cull_distances,
                       bool write_pos_to_clipvertex,
                       bool no_param_export,
                       bool force_vrs,
                       uint64_t outputs_written,
                       ac_nir_prerast_out *out,
                       nir_def *row)
{
   nir_intrinsic_instr *exp[4];
   unsigned exp_num = 0;
   unsigned exp_pos_offset = 0;

   if (dont_export_cull_distances) {
      export_clipdist_mask &= ~BITFIELD_RANGE(b->shader->info.clip_distance_array_size,
                                              b->shader->info.cull_distance_array_size);
   }

   uint64_t mask =
      VARYING_BIT_PSIZ |
      VARYING_BIT_EDGE |
      VARYING_BIT_LAYER |
      VARYING_BIT_VIEWPORT |
      VARYING_BIT_PRIMITIVE_SHADING_RATE;

   /* clear output mask if no one written */
   if (!out->outputs[VARYING_SLOT_PSIZ][0] || !out->infos[VARYING_SLOT_PSIZ].as_sysval_mask)
      outputs_written &= ~VARYING_BIT_PSIZ;
   if (!out->outputs[VARYING_SLOT_EDGE][0] || !out->infos[VARYING_SLOT_EDGE].as_sysval_mask)
      outputs_written &= ~VARYING_BIT_EDGE;
   if (!out->outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE][0] || !out->infos[VARYING_SLOT_PRIMITIVE_SHADING_RATE].as_sysval_mask)
      outputs_written &= ~VARYING_BIT_PRIMITIVE_SHADING_RATE;
   if (!out->outputs[VARYING_SLOT_LAYER][0] || !out->infos[VARYING_SLOT_LAYER].as_sysval_mask)
      outputs_written &= ~VARYING_BIT_LAYER;
   if (!out->outputs[VARYING_SLOT_VIEWPORT][0] || !out->infos[VARYING_SLOT_VIEWPORT].as_sysval_mask)
      outputs_written &= ~VARYING_BIT_VIEWPORT;

   nir_def *clip_dist[8] = {0};

   if (outputs_written & VARYING_BIT_CLIP_VERTEX || write_pos_to_clipvertex) {
      /* Only one condition can be set. */
      assert(!(outputs_written & VARYING_BIT_CLIP_VERTEX) || !write_pos_to_clipvertex);
      /* Convert CLIP_VERTEX to clip distances. */
      assert(!(outputs_written & (VARYING_BIT_CLIP_DIST0 | VARYING_BIT_CLIP_DIST1)));
      gl_varying_slot slot = write_pos_to_clipvertex ? VARYING_SLOT_POS : VARYING_SLOT_CLIP_VERTEX;
      nir_def *vtx = get_export_output(b, out->outputs[slot]);

      u_foreach_bit(i, export_clipdist_mask) {
         nir_def *ucp = nir_load_user_clip_plane(b, .ucp_id = i);
         clip_dist[i] = nir_fdot4(b, vtx, ucp);
      }
   } else {
      /* Gather clip/cull distances. */
      u_foreach_bit(i, export_clipdist_mask) {
         assert(outputs_written & (VARYING_BIT_CLIP_DIST0 << (i / 4)));
         clip_dist[i] = out->outputs[VARYING_SLOT_CLIP_DIST0 + i / 4][i % 4];
      }
   }

   /* If clip/cull distances are sparsely populated or some components are >= 0, pack them. */
   unsigned num = 0;
   u_foreach_bit(i, export_clipdist_mask) {
      clip_dist[num++] = clip_dist[i];
   }
   export_clipdist_mask = BITFIELD_MASK(num);

   if (outputs_written & VARYING_BIT_POS) {
      /* GFX10 (Navi1x) skip POS0 exports if EXEC=0 and DONE=0, causing a hang.
      * Setting valid_mask=1 prevents it and has no other effect.
      */
      const unsigned pos_flags = gfx_level == GFX10 ? AC_EXP_FLAG_VALID_MASK : 0;
      nir_def *pos = get_pos0_output(b, out->outputs[VARYING_SLOT_POS]);

      exp[exp_num] = export(b, pos, row, V_008DFC_SQ_EXP_POS + exp_num, pos_flags, 0xf);
      exp_num++;
   } else {
      exp_pos_offset++;
   }

   if ((outputs_written & mask) || force_vrs) {
      nir_def *zero = nir_imm_float(b, 0);
      nir_def *vec[4] = { zero, zero, zero, zero };
      unsigned write_mask = 0;

      if (outputs_written & VARYING_BIT_PSIZ) {
         vec[0] = out->outputs[VARYING_SLOT_PSIZ][0];
         write_mask |= BITFIELD_BIT(0);
      }

      if (outputs_written & VARYING_BIT_EDGE) {
         vec[1] = nir_umin(b, out->outputs[VARYING_SLOT_EDGE][0], nir_imm_int(b, 1));
         write_mask |= BITFIELD_BIT(1);
      }

      nir_def *rates = NULL;
      if (outputs_written & VARYING_BIT_PRIMITIVE_SHADING_RATE) {
         rates = out->outputs[VARYING_SLOT_PRIMITIVE_SHADING_RATE][0];
      } else if (force_vrs) {
         /* If Pos.W != 1 (typical for non-GUI elements), use coarse shading. */
         nir_def *pos_w = out->outputs[VARYING_SLOT_POS][3];
         pos_w = pos_w ? nir_u2u32(b, pos_w) : nir_imm_float(b, 1.0);
         nir_def *cond = nir_fneu_imm(b, pos_w, 1);
         rates = nir_bcsel(b, cond, nir_load_force_vrs_rates_amd(b), nir_imm_int(b, 0));
      }

      if (rates) {
         vec[1] = nir_ior(b, vec[1], rates);
         write_mask |= BITFIELD_BIT(1);
      }

      if (outputs_written & VARYING_BIT_LAYER) {
         vec[2] = out->outputs[VARYING_SLOT_LAYER][0];
         write_mask |= BITFIELD_BIT(2);
      }

      if (outputs_written & VARYING_BIT_VIEWPORT) {
         if (gfx_level >= GFX9) {
            /* GFX9 has the layer in [10:0] and the viewport index in [19:16]. */
            nir_def *v = nir_ishl_imm(b, out->outputs[VARYING_SLOT_VIEWPORT][0], 16);
            vec[2] = nir_ior(b, vec[2], v);
            write_mask |= BITFIELD_BIT(2);
         } else {
            vec[3] = out->outputs[VARYING_SLOT_VIEWPORT][0];
            write_mask |= BITFIELD_BIT(3);
         }
      }

      exp[exp_num] = export(b, nir_vec(b, vec, 4), row,
                            V_008DFC_SQ_EXP_POS + exp_num + exp_pos_offset,
                            0, write_mask);
      exp_num++;
   }

   for (int i = 0; i < 2; i++) {
      if (export_clipdist_mask & BITFIELD_RANGE(i * 4, 4)) {
         exp[exp_num] = export(b, get_export_output(b, clip_dist + i * 4), row,
                               V_008DFC_SQ_EXP_POS + exp_num + exp_pos_offset, 0,
                               (export_clipdist_mask >> (i * 4)) & 0xf);
         exp_num++;
      }
   }

   if (!exp_num)
      return;

   nir_intrinsic_instr *final_exp = exp[exp_num - 1];

   /* Specify that this is the last export */
   const unsigned final_exp_flags = nir_intrinsic_flags(final_exp);
   nir_intrinsic_set_flags(final_exp, final_exp_flags | AC_EXP_FLAG_DONE);

   /* If a shader has no param exports, rasterization can start before
    * the shader finishes and thus memory stores might not finish before
    * the pixel shader starts.
    */
   if (gfx_level >= GFX10 && no_param_export && b->shader->info.writes_memory) {
      nir_cursor cursor = b->cursor;
      b->cursor = nir_before_instr(&final_exp->instr);
      nir_scoped_memory_barrier(b, SCOPE_DEVICE, NIR_MEMORY_RELEASE,
                                nir_var_mem_ssbo | nir_var_mem_global | nir_var_image);
      b->cursor = cursor;
   }
}

void
ac_nir_export_parameters(nir_builder *b,
                         const uint8_t *param_offsets,
                         uint64_t outputs_written,
                         uint16_t outputs_written_16bit,
                         ac_nir_prerast_out *out)
{
   uint32_t exported_params = 0;

   u_foreach_bit64_two_masks(slot, outputs_written,
                             VARYING_SLOT_VAR0_16BIT, outputs_written_16bit) {
      unsigned offset = param_offsets[slot];
      if (offset > AC_EXP_PARAM_OFFSET_31)
         continue;

      uint32_t write_mask = 0;
      for (int i = 0; i < 4; i++) {
         if (out->outputs[slot][i])
            write_mask |= (out->infos[slot].as_varying_mask & BITFIELD_BIT(i));
      }

      /* no one set this output slot, we can skip the param export */
      if (!write_mask)
         continue;

      /* Since param_offsets[] can map multiple varying slots to the same
       * param export index (that's radeonsi-specific behavior), we need to
       * do this so as not to emit duplicated exports.
       */
      if (exported_params & BITFIELD_BIT(offset))
         continue;

      nir_export_amd(
         b, get_export_output(b, out->outputs[slot]),
         .target = V_008DFC_SQ_EXP_PARAM + offset,
         .enabled_channels = write_mask);
      exported_params |= BITFIELD_BIT(offset);
   }
}

void
ac_nir_store_parameters_to_attr_ring(nir_builder *b,
                                     const uint8_t *param_offsets,
                                     const uint64_t outputs_written,
                                     const uint16_t outputs_written_16bit,
                                     ac_nir_prerast_out *out,
                                     nir_def *num_export_threads_in_wave)
{
   nir_def *attr_rsrc = nir_load_ring_attr_amd(b);

   /* We should always store full vec4s in groups of 8 lanes for the best performance even if
    * some of them are garbage or have unused components, so align the number of export threads
    * to 8.
    */
   nir_def *num_attr_ring_store_threads = nir_iand_imm(b, nir_iadd_imm(b, num_export_threads_in_wave, 7), ~7);

   nir_if *if_attr_ring_store = nir_push_if(b, nir_is_subgroup_invocation_lt_amd(b, num_attr_ring_store_threads));

   nir_def *attr_offset = nir_load_ring_attr_offset_amd(b);
   nir_def *vindex = nir_load_local_invocation_index(b);
   nir_def *voffset = nir_imm_int(b, 0);
   nir_def *undef = nir_undef(b, 1, 32);

   uint32_t exported_params = 0;

   u_foreach_bit64_two_masks(slot, outputs_written,
                             VARYING_SLOT_VAR0_16BIT, outputs_written_16bit) {
      const unsigned offset = param_offsets[slot];

      if (offset > AC_EXP_PARAM_OFFSET_31)
         continue;

      if (!out->infos[slot].as_varying_mask)
         continue;

      if (exported_params & BITFIELD_BIT(offset))
         continue;

      nir_def *comp[4];
      for (unsigned j = 0; j < 4; j++) {
         comp[j] = out->outputs[slot][j] ? out->outputs[slot][j] : undef;
      }

      nir_store_buffer_amd(b, nir_vec(b, comp, 4), attr_rsrc, voffset, attr_offset, vindex,
                           .base = offset * 16,
                           .memory_modes = nir_var_shader_out,
                           .access = ACCESS_COHERENT | ACCESS_IS_SWIZZLED_AMD,
                           .align_mul = 16, .align_offset = 0);

      exported_params |= BITFIELD_BIT(offset);
   }

   nir_pop_if(b, if_attr_ring_store);
}

static int
sort_xfb(const void *_a, const void *_b)
{
   const nir_xfb_output_info *a = (const nir_xfb_output_info *)_a;
   const nir_xfb_output_info *b = (const nir_xfb_output_info *)_b;

   if (a->buffer != b->buffer)
      return a->buffer > b->buffer ? 1 : -1;

   assert(a->offset != b->offset);
   return a->offset > b->offset ? 1 : -1;
}

/* Return XFB info sorted by buffer and offset, so that we can generate vec4
 * stores by iterating over outputs only once.
 */
nir_xfb_info *
ac_nir_get_sorted_xfb_info(const nir_shader *nir)
{
   if (!nir->xfb_info)
      return NULL;

   unsigned xfb_info_size = nir_xfb_info_size(nir->xfb_info->output_count);
   nir_xfb_info *info = rzalloc_size(nir, xfb_info_size);

   memcpy(info, nir->xfb_info, xfb_info_size);
   qsort(info->outputs, info->output_count, sizeof(info->outputs[0]), sort_xfb);
   return info;
}

void
ac_nir_emit_legacy_streamout(nir_builder *b, unsigned stream, nir_xfb_info *info, ac_nir_prerast_out *out)
{
   nir_def *so_vtx_count = nir_ubfe_imm(b, nir_load_streamout_config_amd(b), 16, 7);
   nir_def *tid = nir_load_subgroup_invocation(b);

   nir_push_if(b, nir_ilt(b, tid, so_vtx_count));
   nir_def *so_write_index = nir_load_streamout_write_index_amd(b);

   nir_def *so_buffers[NIR_MAX_XFB_BUFFERS];
   nir_def *so_write_offset[NIR_MAX_XFB_BUFFERS];
   u_foreach_bit(i, info->buffers_written) {
      so_buffers[i] = nir_load_streamout_buffer_amd(b, i);

      unsigned stride = info->buffers[i].stride;
      nir_def *offset = nir_load_streamout_offset_amd(b, i);
      offset = nir_iadd(b, nir_imul_imm(b, nir_iadd(b, so_write_index, tid), stride),
                        nir_imul_imm(b, offset, 4));
      so_write_offset[i] = offset;
   }

   nir_def *zero = nir_imm_int(b, 0);
   unsigned num_values = 0, store_offset = 0, store_buffer_index = 0;
   nir_def *values[4];

   for (unsigned i = 0; i < info->output_count; i++) {
      const nir_xfb_output_info *output = info->outputs + i;
      if (stream != info->buffer_to_stream[output->buffer])
         continue;

      nir_def **output_data = out->outputs[output->location];

      u_foreach_bit(out_comp, output->component_mask) {
         if (!output_data[out_comp])
            continue;

         nir_def *data = output_data[out_comp];

         if (output->data_is_16bit) {
            data = output->high_16bits ? nir_unpack_32_2x16_split_y(b, data)
                                       : nir_unpack_32_2x16_split_x(b, data);

            /* Convert mediump 16-bit outputs to 32 bits for mediump.
             * Vulkan does not allow 8/16bit varyings for streamout.
             */
            if (output->mediump)
               data = nir_convert_to_bit_size(b, data, output->mediump_upconvert_type, 32);
         }

         assert(out_comp >= output->component_offset);
         const unsigned store_comp = out_comp - output->component_offset;
         const unsigned store_comp_offset = output->offset + store_comp * 4;
         const bool has_hole = store_offset + num_values * 4 != store_comp_offset;

         /* Flush the gathered components to memory as a vec4 store or less if there is a hole. */
         if (num_values && (num_values == 4 || store_buffer_index != output->buffer || has_hole)) {
            nir_store_buffer_amd(b, nir_vec(b, values, num_values), so_buffers[store_buffer_index],
                                 so_write_offset[store_buffer_index], zero, zero,
                                 .base = store_offset,
                                 .access = ACCESS_NON_TEMPORAL);
            num_values = 0;
         }

         /* Initialize the buffer index and offset if we are beginning a new vec4 store. */
         if (num_values == 0) {
            store_buffer_index = output->buffer;
            store_offset = store_comp_offset;
         }

         values[num_values++] = data;
      }
   }

   if (num_values) {
      /* Flush the remaining components to memory (as an up to vec4 store) */
      nir_store_buffer_amd(b, nir_vec(b, values, num_values), so_buffers[store_buffer_index],
                           so_write_offset[store_buffer_index], zero, zero,
                           .base = store_offset,
                           .access = ACCESS_NON_TEMPORAL);
   }

   nir_pop_if(b, NULL);
}

static nir_def *
ac_nir_accum_ior(nir_builder *b, nir_def *accum_result, nir_def *new_term)
{
   return accum_result ? nir_ior(b, accum_result, new_term) : new_term;
}

bool
ac_nir_gs_shader_query(nir_builder *b,
                       bool has_gen_prim_query,
                       bool has_gs_invocations_query,
                       bool has_gs_primitives_query,
                       unsigned num_vertices_per_primitive,
                       unsigned wave_size,
                       nir_def *vertex_count[4],
                       nir_def *primitive_count[4])
{
   nir_def *pipeline_query_enabled = NULL;
   nir_def *prim_gen_query_enabled = NULL;
   nir_def *any_query_enabled = NULL;

   if (has_gen_prim_query) {
      prim_gen_query_enabled = nir_load_prim_gen_query_enabled_amd(b);
      any_query_enabled = ac_nir_accum_ior(b, any_query_enabled, prim_gen_query_enabled);
   }

   if (has_gs_invocations_query || has_gs_primitives_query) {
      pipeline_query_enabled = nir_load_pipeline_stat_query_enabled_amd(b);
      any_query_enabled = ac_nir_accum_ior(b, any_query_enabled, pipeline_query_enabled);
   }

   if (!any_query_enabled) {
      /* has no query */
      return false;
   }

   nir_if *if_shader_query = nir_push_if(b, any_query_enabled);

   nir_def *active_threads_mask = nir_ballot(b, 1, wave_size, nir_imm_true(b));
   nir_def *num_active_threads = nir_bit_count(b, active_threads_mask);

   /* Calculate the "real" number of emitted primitives from the emitted GS vertices and primitives.
    * GS emits points, line strips or triangle strips.
    * Real primitives are points, lines or triangles.
    */
   nir_def *num_prims_in_wave[4] = {0};
   u_foreach_bit (i, b->shader->info.gs.active_stream_mask) {
      assert(vertex_count[i] && primitive_count[i]);

      nir_scalar vtx_cnt = nir_get_scalar(vertex_count[i], 0);
      nir_scalar prm_cnt = nir_get_scalar(primitive_count[i], 0);

      if (nir_scalar_is_const(vtx_cnt) && nir_scalar_is_const(prm_cnt)) {
         unsigned gs_vtx_cnt = nir_scalar_as_uint(vtx_cnt);
         unsigned gs_prm_cnt = nir_scalar_as_uint(prm_cnt);
         unsigned total_prm_cnt = gs_vtx_cnt - gs_prm_cnt * (num_vertices_per_primitive - 1u);
         if (total_prm_cnt == 0)
            continue;

         num_prims_in_wave[i] = nir_imul_imm(b, num_active_threads, total_prm_cnt);
      } else {
         nir_def *gs_vtx_cnt = vtx_cnt.def;
         nir_def *gs_prm_cnt = prm_cnt.def;
         if (num_vertices_per_primitive > 1)
            gs_prm_cnt = nir_iadd(b, nir_imul_imm(b, gs_prm_cnt, -1u * (num_vertices_per_primitive - 1)), gs_vtx_cnt);
         num_prims_in_wave[i] = nir_reduce(b, gs_prm_cnt, .reduction_op = nir_op_iadd);
      }
   }

   /* Store the query result to query result using an atomic add. */
   nir_if *if_first_lane = nir_push_if(b, nir_elect(b, 1));
   {
      if (has_gs_invocations_query || has_gs_primitives_query) {
         nir_if *if_pipeline_query = nir_push_if(b, pipeline_query_enabled);
         {
            nir_def *count = NULL;

            /* Add all streams' number to the same counter. */
            for (int i = 0; i < 4; i++) {
               if (num_prims_in_wave[i]) {
                  if (count)
                     count = nir_iadd(b, count, num_prims_in_wave[i]);
                  else
                     count = num_prims_in_wave[i];
               }
            }

            if (has_gs_primitives_query && count)
               nir_atomic_add_gs_emit_prim_count_amd(b, count);

            if (has_gs_invocations_query)
               nir_atomic_add_shader_invocation_count_amd(b, num_active_threads);
         }
         nir_pop_if(b, if_pipeline_query);
      }

      if (has_gen_prim_query) {
         nir_if *if_prim_gen_query = nir_push_if(b, prim_gen_query_enabled);
         {
            /* Add to the counter for this stream. */
            for (int i = 0; i < 4; i++) {
               if (num_prims_in_wave[i])
                  nir_atomic_add_gen_prim_count_amd(b, num_prims_in_wave[i], .stream_id = i);
            }
         }
         nir_pop_if(b, if_prim_gen_query);
      }
   }
   nir_pop_if(b, if_first_lane);

   nir_pop_if(b, if_shader_query);
   return true;
}

nir_def *
ac_nir_pack_ngg_prim_exp_arg(nir_builder *b, unsigned num_vertices_per_primitives,
                             nir_def *vertex_indices[3], nir_def *is_null_prim,
                             enum amd_gfx_level gfx_level)
{
   nir_def *arg = nir_load_initial_edgeflags_amd(b);

   for (unsigned i = 0; i < num_vertices_per_primitives; ++i) {
      assert(vertex_indices[i]);
      arg = nir_ior(b, arg, nir_ishl_imm(b, vertex_indices[i],
                                         (gfx_level >= GFX12 ? 9u : 10u) * i));
   }

   if (is_null_prim) {
      if (is_null_prim->bit_size == 1)
         is_null_prim = nir_b2i32(b, is_null_prim);
      assert(is_null_prim->bit_size == 32);
      arg = nir_ior(b, arg, nir_ishl_imm(b, is_null_prim, 31u));
   }

   return arg;
}

void
ac_nir_clamp_vertex_color_outputs(nir_builder *b, ac_nir_prerast_out *out)
{
   /* Clamp color outputs. */
   if (!(b->shader->info.outputs_written & (VARYING_BIT_COL0 | VARYING_BIT_COL1 |
                                            VARYING_BIT_BFC0 | VARYING_BIT_BFC1)))
      return;

   unsigned i = 0;
   nir_def **color_channels[16] = {0};
   nir_def *color_channels_clamped[16] = {0};

   nir_if *if_clamp = nir_push_if(b, nir_load_clamp_vertex_color_amd(b));
   {
      for (unsigned slot = 0; slot < NUM_TOTAL_VARYING_SLOTS; slot++) {
         unsigned clamp_mask = out->infos[slot].clamp_components_mask;
         u_foreach_bit(comp, clamp_mask) {
            assert(i < 16 && comp < 4);
            color_channels[i] = &out->outputs[slot][comp];
            color_channels_clamped[i] = nir_fsat(b, out->outputs[slot][comp]);
            i++;
         }
      }
   }
   nir_pop_if(b, if_clamp);

   for (unsigned i = 0; i < 16; i++) {
      if (color_channels[i]) {
         *color_channels[i] = nir_if_phi(b, color_channels_clamped[i], *color_channels[i]);
      }
   }
}

static void
ac_nir_ngg_alloc_vertices_fully_culled_workaround(nir_builder *b,
                                                  nir_def *num_vtx,
                                                  nir_def *num_prim)
{
   /* HW workaround for a GPU hang with 100% culling on GFX10.
    * We always have to export at least 1 primitive.
    * Export a degenerate triangle using vertex 0 for all 3 vertices.
    *
    * NOTE: We rely on the caller to set the vertex count also to 0 when the primitive count is 0.
    */
   nir_def *is_prim_cnt_0 = nir_ieq_imm(b, num_prim, 0);
   nir_if *if_prim_cnt_0 = nir_push_if(b, is_prim_cnt_0);
   {
      nir_def *one = nir_imm_int(b, 1);
      nir_sendmsg_amd(b, nir_ior(b, nir_ishl_imm(b, one, 12), one), .base = AC_SENDMSG_GS_ALLOC_REQ);

      nir_def *tid = nir_load_subgroup_invocation(b);
      nir_def *is_thread_0 = nir_ieq_imm(b, tid, 0);
      nir_if *if_thread_0 = nir_push_if(b, is_thread_0);
      {
         /* The vertex indices are 0, 0, 0. */
         nir_export_amd(b, nir_imm_zero(b, 4, 32),
                        .target = V_008DFC_SQ_EXP_PRIM,
                        .flags = AC_EXP_FLAG_DONE,
                        .enabled_channels = 1);

         /* The HW culls primitives with NaN. -1 is also NaN and can save
          * a dword in binary code by inlining constant.
          */
         nir_export_amd(b, nir_imm_ivec4(b, -1, -1, -1, -1),
                        .target = V_008DFC_SQ_EXP_POS,
                        .flags = AC_EXP_FLAG_DONE,
                        .enabled_channels = 0xf);
      }
      nir_pop_if(b, if_thread_0);
   }
   nir_push_else(b, if_prim_cnt_0);
   {
      nir_sendmsg_amd(b, nir_ior(b, nir_ishl_imm(b, num_prim, 12), num_vtx), .base = AC_SENDMSG_GS_ALLOC_REQ);
   }
   nir_pop_if(b, if_prim_cnt_0);
}

/**
 * Emits code for allocating space for vertices and primitives for NGG shaders.
 * The caller should only call this conditionally on wave 0.
 * When either the vertex or primitive count is 0, both should be set to 0.
 */
void
ac_nir_ngg_alloc_vertices_and_primitives(nir_builder *b,
                                         nir_def *num_vtx,
                                         nir_def *num_prim,
                                         bool fully_culled_workaround)
{
   if (fully_culled_workaround) {
      ac_nir_ngg_alloc_vertices_fully_culled_workaround(b, num_vtx, num_prim);
      return;
   }

   /* Send GS Alloc Request message from the first wave of the group to SPI.
    * Message payload (in the m0 register) is:
    * - bits 0..10: number of vertices in group
    * - bits 12..22: number of primitives in group
    */
   nir_sendmsg_amd(b, nir_ior(b, nir_ishl_imm(b, num_prim, 12), num_vtx), .base = AC_SENDMSG_GS_ALLOC_REQ);
}

void
ac_nir_create_output_phis(nir_builder *b,
                          const uint64_t outputs_written,
                          const uint64_t outputs_written_16bit,
                          ac_nir_prerast_out *out)
{
   nir_def *undef = nir_undef(b, 1, 32); /* inserted at the start of the shader */

   u_foreach_bit64_two_masks(slot, outputs_written,
                             VARYING_SLOT_VAR0_16BIT, outputs_written_16bit) {
      for (unsigned j = 0; j < 4; j++) {
         if (out->outputs[slot][j])
            out->outputs[slot][j] = nir_if_phi(b, out->outputs[slot][j], undef);
      }
   }
}

static nir_def *
write_values_to_lanes(nir_builder *b, nir_def **values, unsigned lane_mask)
{
   nir_def *lanes = nir_imm_int(b, 0);

   u_foreach_bit(i, lane_mask) {
      lanes = nir_write_invocation_amd(b, lanes, values[i], nir_imm_int(b, i));
   }
   return lanes;
}

static nir_def *
read_values_from_4_lanes(nir_builder *b, nir_def *values, unsigned lane_mask)
{
   nir_def *undef = nir_undef(b, 1, 32);
   nir_def *per_lane[4] = {undef, undef, undef, undef};

   u_foreach_bit(i, lane_mask) {
      per_lane[i] = nir_read_invocation(b, values, nir_imm_int(b, i));
   }
   return nir_vec(b, per_lane, 4);
}

void
ac_nir_ngg_build_streamout_buffer_info(nir_builder *b,
                                       nir_xfb_info *info,
                                       enum amd_gfx_level gfx_level,
                                       bool has_xfb_prim_query,
                                       bool use_gfx12_xfb_intrinsic,
                                       nir_def *scratch_base,
                                       nir_def *tid_in_tg,
                                       nir_def *gen_prim[4],
                                       nir_def *so_buffer_ret[4],
                                       nir_def *buffer_offsets_ret[4],
                                       nir_def *emit_prim_ret[4])
{
   nir_def *prim_stride[4] = {0};
   nir_def *undef = nir_undef(b, 1, 32);

   /* For radeonsi which pass this value by arg when VS. Streamout need accurate
    * num-vert-per-prim for writing correct amount of data to buffer.
    */
   nir_def *num_vert_per_prim = nir_load_num_vertices_per_primitive_amd(b);

   u_foreach_bit(buffer, info->buffers_written) {
      assert(info->buffers[buffer].stride);

      prim_stride[buffer] =
         nir_imul_imm(b, num_vert_per_prim, info->buffers[buffer].stride);
      so_buffer_ret[buffer] = nir_load_streamout_buffer_amd(b, .base = buffer);
   }

   nir_if *if_invocation_0 = nir_push_if(b, nir_ieq_imm(b, tid_in_tg, 0));
   {
      nir_def *any_buffer_valid = nir_imm_false(b);
      nir_def *workgroup_buffer_sizes[4];

      for (unsigned buffer = 0; buffer < 4; buffer++) {
         if (info->buffers_written & BITFIELD_BIT(buffer)) {
            nir_def *buffer_size = nir_channel(b, so_buffer_ret[buffer], 2);
            /* In radeonsi, we may not know if a feedback buffer has been bound when
             * compile time, so have to check buffer size in runtime to disable the
             * GDS update for unbind buffer to prevent the case that previous draw
             * compiled with streamout but does not bind feedback buffer miss update
             * GDS which will affect current draw's streamout.
             */
            nir_def *buffer_valid = nir_ine_imm(b, buffer_size, 0);
            nir_def *inc_buffer_size =
               nir_imul(b, gen_prim[info->buffer_to_stream[buffer]], prim_stride[buffer]);
            workgroup_buffer_sizes[buffer] =
               nir_bcsel(b, buffer_valid, inc_buffer_size, nir_imm_int(b, 0));
            any_buffer_valid = nir_ior(b, any_buffer_valid, buffer_valid);
         } else
            workgroup_buffer_sizes[buffer] = undef;
      }

      nir_def *buffer_offsets = NULL, *xfb_state_address = NULL, *xfb_voffset = NULL;

      /* Get current global offset of buffer and increase by amount of
       * workgroup buffer size. This is an ordered operation sorted by
       * ordered_id; Each buffer info is in a channel of a vec4.
       */
      if (gfx_level >= GFX12) {
         nir_pop_if(b, if_invocation_0);

         for (unsigned buffer = 0; buffer < 4; buffer++)
            workgroup_buffer_sizes[buffer] = nir_if_phi(b, workgroup_buffer_sizes[buffer], undef);
         any_buffer_valid = nir_if_phi(b, any_buffer_valid, nir_undef(b, 1, 1));

         /* These must be set after nir_pop_if and phis. */
         xfb_state_address = nir_load_xfb_state_address_gfx12_amd(b);
         xfb_voffset = nir_imul_imm(b, tid_in_tg, 8);

         nir_if *if_4lanes = nir_push_if(b, nir_iand(b, any_buffer_valid, nir_ult_imm(b, tid_in_tg, 4)));
         {
            /* Move workgroup buffer sizes from SGPRs to the first 4 lanes. */
            nir_def *workgroup_buffer_size_per_lane =
               write_values_to_lanes(b, workgroup_buffer_sizes, info->buffers_written);
            nir_def *ordered_id = nir_load_ordered_id_amd(b);

            /* The atomic value for the 4 lanes is:
             *    lane 0: uvec2(ordered_id, workgroup_buffer_size0)
             *    lane 1: uvec2(ordered_id, workgroup_buffer_size1)
             *    lane 2: uvec2(ordered_id, workgroup_buffer_size2)
             *    lane 3: uvec2(ordered_id, workgroup_buffer_size3)
             */
            nir_def *atomic_src = nir_pack_64_2x32_split(b, ordered_id,
                                                         workgroup_buffer_size_per_lane);

            /* The memory layout of the xfb state is:
             *    struct {
             *       unsigned ordered_id;
             *       unsigned dwords_written0;
             *       unsigned ordered_id;
             *       unsigned dwords_written1;
             *       unsigned ordered_id;
             *       unsigned dwords_written2;
             *       unsigned ordered_id;
             *       unsigned dwords_written3;
             *    };
             *
             * Notes:
             * - global_atomic_ordered_add_b64 is semantically a 64-bit atomic, requiring 8-byte
             *   address alignment, even though it operates on a pair of 32-bit values.
             * - The whole structure is updated at once by issuing the atomic from 4 lanes
             *   with 8-byte address increments.
             * - The whole structure should be entirely within one 64B block of memory
             *   for performance. (the address bits above 64B should not differ between lanes)
             */
            nir_def *buffer_offset_per_lane;

            /* The gfx12 intrinsic inserts hand-written assembly producing better code than current
             * LLVM.
             */
            if (use_gfx12_xfb_intrinsic) {
               buffer_offset_per_lane =
                  nir_ordered_add_loop_gfx12_amd(b, xfb_state_address, xfb_voffset, ordered_id,
                                                 atomic_src);

               /* Move the buffer offsets from the 4 lanes to lane 0. */
               buffer_offsets = read_values_from_4_lanes(b, buffer_offset_per_lane, info->buffers_written);
            } else {
               /* The NIR version of the above using nir_atomic_op_ordered_add_gfx12_amd. */
               enum { NUM_ATOMICS_IN_FLIGHT = 6 };

               nir_variable *result_ring[NUM_ATOMICS_IN_FLIGHT] = {0};
               for (unsigned i = 0; i < NUM_ATOMICS_IN_FLIGHT; i++)
                  result_ring[i] = nir_local_variable_create(b->impl, glsl_uint64_t_type(), "result");

               /* Issue the first N-1 atomics. The shader must not wait because we want them to be
                * pipelined. It will only wait for the oldest atomic in the NIR loop.
                */
               for (unsigned i = 0; i < NUM_ATOMICS_IN_FLIGHT - 1; i++) {
                  nir_store_var(b, result_ring[i],
                                nir_global_atomic_amd(b, 64, xfb_state_address, atomic_src, xfb_voffset,
                                                      .atomic_op = nir_atomic_op_ordered_add_gfx12_amd), 0x1);
                  ac_nir_sleep(b, 24);
               }

               nir_variable *buffer_offsets_var =
                  nir_local_variable_create(b->impl, glsl_vec4_type(), "buffer_offset_per_lane");

               nir_loop *loop = nir_push_loop(b);
               {
                  nir_loop_add_continue_construct(loop);

                  for (unsigned i = 0; i < NUM_ATOMICS_IN_FLIGHT; i++) {
                     int issue_index = (NUM_ATOMICS_IN_FLIGHT - 1 + i) % NUM_ATOMICS_IN_FLIGHT;
                     int read_index = i;

                     /* Issue (or repeat) the atomic. */
                     nir_store_var(b, result_ring[issue_index],
                                   nir_global_atomic_amd(b, 64, xfb_state_address, atomic_src, xfb_voffset,
                                                         .atomic_op = nir_atomic_op_ordered_add_gfx12_amd), 0x1);

                     /* Break if the oldest atomic succeeded in incrementing the offsets. */
                     nir_def *oldest_result = nir_load_var(b, result_ring[read_index]);
                     nir_def *loaded_ordered_id = nir_unpack_64_2x32_split_x(b, oldest_result);

                     /* Debug: Write the vec4 into a shader log ring buffer. */
#if 0
                     nir_def *loaded_dwords_written = nir_unpack_64_2x32_split_y(b, oldest_result);
                     ac_nir_store_debug_log_amd(b, nir_vec4(b, nir_u2u32(b, xfb_state_address),
                                                            ordered_id, loaded_ordered_id,
                                                            loaded_dwords_written));
#endif

                     nir_def *continue_if = nir_ieq(b, loaded_ordered_id, ordered_id);
                     continue_if = nir_inot(b, nir_vote_any(b, 1, continue_if));
                     nir_push_if(b, continue_if);
                  }
                  nir_jump(b, nir_jump_continue);

                  for (unsigned i = 0; i < NUM_ATOMICS_IN_FLIGHT; i++) {
                     int read_index = NUM_ATOMICS_IN_FLIGHT - 1 - i;
                     nir_push_else(b, NULL);
                     {
                        nir_def *result = nir_load_var(b, result_ring[read_index]);
                        buffer_offset_per_lane = nir_unpack_64_2x32_split_y(b, result);
                        buffer_offsets = read_values_from_4_lanes(b, buffer_offset_per_lane, info->buffers_written);
                        nir_store_var(b, buffer_offsets_var, buffer_offsets, info->buffers_written);
                     }
                     nir_pop_if(b, NULL);
                  }
                  nir_jump(b, nir_jump_break);
               }
               nir_pop_loop(b, loop);
               buffer_offsets = nir_load_var(b, buffer_offsets_var);
            }
         }
         nir_pop_if(b, if_4lanes);
         buffer_offsets = nir_if_phi(b, buffer_offsets, nir_undef(b, 4, 32));

         if_invocation_0 = nir_push_if(b, nir_ieq_imm(b, tid_in_tg, 0));
      } else {
         nir_def *ordered_id = nir_load_ordered_id_amd(b);
         buffer_offsets =
            nir_ordered_xfb_counter_add_gfx11_amd(b, ordered_id,
                                                  nir_vec(b, workgroup_buffer_sizes, 4),
                                                  /* mask of buffers to update */
                                                  .write_mask = info->buffers_written);
      }

      nir_def *emit_prim[4];
      memcpy(emit_prim, gen_prim, 4 * sizeof(nir_def *));

      nir_def *any_overflow = nir_imm_false(b);
      nir_def *overflow_amount[4] = {undef, undef, undef, undef};

      u_foreach_bit(buffer, info->buffers_written) {
         nir_def *buffer_size = nir_channel(b, so_buffer_ret[buffer], 2);

         /* Only consider overflow for valid feedback buffers because
          * otherwise the ordered operation above (GDS atomic return) might
          * return non-zero offsets for invalid buffers.
          */
         nir_def *buffer_valid = nir_ine_imm(b, buffer_size, 0);
         nir_def *buffer_offset = nir_channel(b, buffer_offsets, buffer);
         buffer_offset = nir_bcsel(b, buffer_valid, buffer_offset, nir_imm_int(b, 0));

         nir_def *remain_size = nir_isub(b, buffer_size, buffer_offset);
         nir_def *remain_prim = nir_idiv(b, remain_size, prim_stride[buffer]);
         nir_def *overflow = nir_ilt(b, buffer_size, buffer_offset);

         any_overflow = nir_ior(b, any_overflow, overflow);
         nir_def *new_buffer_offset = nir_iadd(b, buffer_offset,
                                               workgroup_buffer_sizes[buffer]);
         overflow_amount[buffer] = nir_imax(b, nir_imm_int(b, 0),
                                            nir_isub(b, new_buffer_offset, buffer_size));

         unsigned stream = info->buffer_to_stream[buffer];
         /* when previous workgroup overflow, we can't emit any primitive */
         emit_prim[stream] = nir_bcsel(
            b, overflow, nir_imm_int(b, 0),
            /* we can emit part primitives, limited by smallest buffer */
            nir_imin(b, emit_prim[stream], remain_prim));

         /* Save to LDS for being accessed by other waves in this workgroup. */
         nir_store_shared(b, buffer_offset, scratch_base, .base = buffer * 4);
      }

      /* We have to fix up the streamout offsets if we overflowed because they determine
       * the vertex count for DrawTransformFeedback.
       */
      if (gfx_level >= GFX12) {
         nir_pop_if(b, if_invocation_0);

         any_overflow = nir_if_phi(b, any_overflow, nir_undef(b, 1, 1));
         for (unsigned buffer = 0; buffer < 4; buffer++)
            overflow_amount[buffer] = nir_if_phi(b, overflow_amount[buffer], undef);
         for (unsigned stream = 0; stream < 4; stream++) {
            if (emit_prim[stream])
               emit_prim[stream] = nir_if_phi(b, emit_prim[stream], undef);
         }

         nir_if *if_any_overflow_4_lanes =
            nir_push_if(b, nir_iand(b, any_overflow, nir_ult_imm(b, tid_in_tg, 4)));
         {
            /* Move overflow amounts from SGPRs to the first 4 lanes. */
            nir_def *overflow_amount_per_lane =
               write_values_to_lanes(b, overflow_amount, info->buffers_written);

            nir_global_atomic_amd(b, 32, xfb_state_address, nir_ineg(b, overflow_amount_per_lane),
                                  xfb_voffset, .base = 4, .atomic_op = nir_atomic_op_iadd);
         }
         nir_pop_if(b, if_any_overflow_4_lanes);

         if_invocation_0 = nir_push_if(b, nir_ieq_imm(b, tid_in_tg, 0));
      } else {
         nir_if *if_any_overflow = nir_push_if(b, any_overflow);
         nir_xfb_counter_sub_gfx11_amd(b, nir_vec(b, overflow_amount, 4),
                                       /* mask of buffers to update */
                                       .write_mask = info->buffers_written);
         nir_pop_if(b, if_any_overflow);
      }

      /* Save to LDS for being accessed by other waves in this workgroup. */
      u_foreach_bit(stream, info->streams_written) {
         nir_store_shared(b, emit_prim[stream], scratch_base, .base = 16 + stream * 4);
      }

      /* Update shader query. */
      if (has_xfb_prim_query) {
         nir_if *if_shader_query = nir_push_if(b, nir_load_prim_xfb_query_enabled_amd(b));
         {
            for (unsigned stream = 0; stream < 4; stream++) {
               if (info->streams_written & BITFIELD_BIT(stream))
                  nir_atomic_add_xfb_prim_count_amd(b, emit_prim[stream], .stream_id = stream);
            }
         }
         nir_pop_if(b, if_shader_query);
      }
   }
   nir_pop_if(b, if_invocation_0);

   nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                      .memory_scope = SCOPE_WORKGROUP,
                      .memory_semantics = NIR_MEMORY_ACQ_REL,
                      .memory_modes = nir_var_mem_shared);

   /* Fetch the per-buffer offsets in all waves. */
   u_foreach_bit(buffer, info->buffers_written) {
      buffer_offsets_ret[buffer] =
         nir_load_shared(b, 1, 32, scratch_base, .base = buffer * 4);
   }

   /* Fetch the per-stream emit prim in all waves. */
   u_foreach_bit(stream, info->streams_written) {
      emit_prim_ret[stream] =
         nir_load_shared(b, 1, 32, scratch_base, .base = 16 + stream * 4);
   }
}

static unsigned
ac_nir_get_gs_out_lds_offset(ac_nir_prerast_out *pr_out, gl_varying_slot slot, unsigned component)
{
   assert(component < 4);
   unsigned lds_slot_offset = pr_out->infos[slot].packed_slot_gs_out_offset;
   unsigned lds_component_mask = pr_out->infos[slot].components_mask & ~pr_out->infos[slot].const_mask;

   return lds_slot_offset + util_bitcount(lds_component_mask & BITFIELD_MASK(component)) * 4;
}

static unsigned
ac_nir_ngg_get_xfb_lds_offset(ac_nir_prerast_out *pr_out, gl_varying_slot slot, unsigned component)
{
   assert(component < 4);
   unsigned lds_slot_offset = pr_out->infos[slot].packed_slot_xfb_lds_offset;
   unsigned lds_component_mask = pr_out->infos[slot].xfb_lds_components_mask & ~pr_out->infos[slot].const_mask;

   return lds_slot_offset + util_bitcount(lds_component_mask & BITFIELD_MASK(component)) * 4;
}

bool
ac_nir_is_const_output(ac_nir_prerast_out *pr_out, gl_varying_slot slot, unsigned component)
{
   return pr_out->infos[slot].const_mask & BITFIELD_BIT(component);
}

nir_def *
ac_nir_get_const_output(nir_builder *b, ac_nir_prerast_out *pr_out, gl_varying_slot slot,
                        unsigned component)
{
   if (!ac_nir_is_const_output(pr_out, slot, component))
      return NULL;

   return nir_imm_intN_t(b, pr_out->const_values[slot][component], 32);
}

void
ac_nir_store_shared_xfb(nir_builder *b, nir_def *value, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                        gl_varying_slot slot, unsigned component)
{
   assert(value->num_components == 1);
   assert(value->bit_size == 32);

   if (ac_nir_is_const_output(pr_out, slot, component))
      return;

   unsigned offset = ac_nir_ngg_get_xfb_lds_offset(pr_out, slot, component);
   nir_store_shared(b, value, vtxptr, .base = offset, .align_mul = 4);
}

nir_def *
ac_nir_load_shared_xfb(nir_builder *b, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                       gl_varying_slot slot, unsigned component)
{
   nir_def *const_val = ac_nir_get_const_output(b, pr_out, slot, component);
   if (const_val)
      return const_val;

   unsigned offset = ac_nir_ngg_get_xfb_lds_offset(pr_out, slot, component);
   return nir_load_shared(b, 1, 32, vtxptr, .base = offset, .align_mul = 4);
}

void
ac_nir_store_shared_gs_out(nir_builder *b, nir_def *value, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                           gl_varying_slot slot, unsigned component)
{
   assert(value->num_components == 1);
   assert(value->bit_size == 32);

   if (ac_nir_is_const_output(pr_out, slot, component))
      return;

   unsigned offset = ac_nir_get_gs_out_lds_offset(pr_out, slot, component);
   nir_store_shared(b, value, vtxptr, .base = offset, .align_mul = 4);
}

nir_def *
ac_nir_load_shared_gs_out(nir_builder *b, nir_def *vtxptr, ac_nir_prerast_out *pr_out,
                          gl_varying_slot slot, unsigned component)
{
   nir_def *const_val = ac_nir_get_const_output(b, pr_out, slot, component);
   if (const_val)
      return const_val;

   unsigned offset = ac_nir_get_gs_out_lds_offset(pr_out, slot, component);
   return nir_load_shared(b, 1, 32, vtxptr, .base = offset, .align_mul = 4);
}

void
ac_nir_ngg_build_streamout_vertex(nir_builder *b, nir_xfb_info *info,
                                  unsigned stream, nir_def *so_buffer[4],
                                  nir_def *buffer_offsets[4],
                                  unsigned vertex_index, nir_def *vtx_lds_addr,
                                  ac_nir_prerast_out *pr_out)
{
   unsigned vertex_offset[NIR_MAX_XFB_BUFFERS] = {0};

   u_foreach_bit(buffer, info->buffers_written) {
      /* We use imm_offset for the vertex offset within a primitive, and GFX11 only supports
       * 12-bit unsigned imm_offset. (GFX12 supports 24-bit signed imm_offset)
       */
      assert(info->buffers[buffer].stride * 3 < 4096);
      vertex_offset[buffer] = vertex_index * info->buffers[buffer].stride;
   }

   nir_def *zero = nir_imm_int(b, 0);
   unsigned num_values = 0, store_offset = 0, store_buffer_index = 0;
   nir_def *values[4];

   for (unsigned i = 0; i < info->output_count; i++) {
      nir_xfb_output_info *out = info->outputs + i;
      if (!out->component_mask || info->buffer_to_stream[out->buffer] != stream)
         continue;

      unsigned count = util_bitcount(out->component_mask);

      for (unsigned comp = 0; comp < count; comp++) {
         nir_def *data = ac_nir_load_shared_xfb(b, vtx_lds_addr, pr_out, out->location,
                                                out->component_offset + comp);

         if (out->data_is_16bit) {
            data = out->high_16bits ? nir_unpack_32_2x16_split_y(b, data)
                                    : nir_unpack_32_2x16_split_x(b, data);

            /* Convert mediump 16-bit outputs to 32 bits for mediump.
             * Vulkan does not allow 8/16bit varyings for streamout.
             */
            if (out->mediump)
               data = nir_convert_to_bit_size(b, data, out->mediump_upconvert_type, 32);
         }

         const unsigned store_comp_offset = out->offset + comp * 4;
         const bool has_hole = store_offset + num_values * 4 != store_comp_offset;

         /* Flush the gathered components to memory as a vec4 store or less if there is a hole. */
         if (num_values && (num_values == 4 || store_buffer_index != out->buffer || has_hole)) {
            nir_store_buffer_amd(b, nir_vec(b, values, num_values), so_buffer[store_buffer_index],
                                 buffer_offsets[store_buffer_index], zero, zero,
                                 .base = vertex_offset[store_buffer_index] + store_offset,
                                 .access = ACCESS_NON_TEMPORAL);
            num_values = 0;
         }

         /* Initialize the buffer index and offset if we are beginning a new vec4 store. */
         if (num_values == 0) {
            store_buffer_index = out->buffer;
            store_offset = store_comp_offset;
         }

         values[num_values++] = data;
      }
   }

   if (num_values) {
      /* Flush the remaining components to memory (as an up to vec4 store) */
      nir_store_buffer_amd(b, nir_vec(b, values, num_values), so_buffer[store_buffer_index],
                           buffer_offsets[store_buffer_index], zero, zero,
                           .base = vertex_offset[store_buffer_index] + store_offset,
                           .access = ACCESS_NON_TEMPORAL);
   }
}

/* Determine optimal output packing based on component masks, and set packed offsets. */
void
ac_nir_compute_prerast_packed_output_info(ac_nir_prerast_out *pr_out)
{
   unsigned gs_out_offset = 0;
   unsigned xfb_lds_offset = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(pr_out->infos); i++) {
      assert(gs_out_offset < BITFIELD_BIT(12));
      assert(xfb_lds_offset < BITFIELD_BIT(12));
      pr_out->infos[i].packed_slot_gs_out_offset = gs_out_offset;
      pr_out->infos[i].packed_slot_xfb_lds_offset = xfb_lds_offset;

      if (pr_out->infos[i].components_mask & ~pr_out->infos[i].const_mask) {
         gs_out_offset += util_bitcount(pr_out->infos[i].components_mask &
                                        ~pr_out->infos[i].const_mask) * 4;
      }
      if (pr_out->infos[i].xfb_lds_components_mask & ~pr_out->infos[i].const_mask) {
         xfb_lds_offset += util_bitcount(pr_out->infos[i].xfb_lds_components_mask &
                                         ~pr_out->infos[i].const_mask) * 4;
      }
   }

   assert(gs_out_offset < BITFIELD_BIT(16));
   assert(xfb_lds_offset < BITFIELD_BIT(16));
   pr_out->total_packed_gs_out_size = gs_out_offset;
   pr_out->total_packed_xfb_lds_size = xfb_lds_offset;
}

unsigned
ac_nir_gs_output_component_mask_with_stream(ac_nir_prerast_per_output_info *info, unsigned stream)
{
   unsigned mask = info->components_mask;
   if (!mask)
      return 0;

   /* clear component when not requested stream */
   for (int i = 0; i < 4; i++) {
      if (((info->stream >> (i * 2)) & 3) != stream)
         mask &= ~(1 << i);
   }

   return mask;
}
