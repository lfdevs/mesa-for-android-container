/*
 * Copyright (C) 2025-2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "bifrost_compile.h"
#include "pan_nir.h"
#include "nir_builder.h"

#include "panfrost/model/pan_model.h"
#include "bifrost/valhall/valhall.h"
#include "util/bitscan.h"

struct lower_vs_outputs_ctx {
   unsigned arch;
   const struct pan_varying_layout *varying_layout;
   bool has_idvs;
   bool has_extended_fifo;

   nir_variable *variables[PAN_MAX_VARYINGS];
   uint8_t per_view_written[PAN_MAX_VARYINGS];
   unsigned used_buckets;
   bool uses_multiview;
};

static bool
valhal_writes_extended_fifo(uint64_t outputs_written,
                            bool no_psiz, bool multiview)
{
   uint64_t ex_fifo_written = outputs_written & VALHAL_EX_FIFO_VARYING_BITS;
   if (ex_fifo_written == 0)
      return false;

   /* Multiview shaders depend on the FIFO format for indexing per-view
    * output writes. We don't currently patch these offsets in the no_psiz
    * variant, so we need the extended format, regardless of point size.
    */
   if (multiview)
      return true;

   /* If we're not rendering in points mode, the no_psiz variant has point
    * size write patched out for us.
    */
   if (no_psiz)
      ex_fifo_written &= ~VARYING_BIT_PSIZ;

   return ex_fifo_written != 0;
}

static void
build_attr_buf_write(struct nir_builder *b, nir_def *data, uint32_t idx,
                     uint32_t view_index,
                     const struct lower_vs_outputs_ctx *ctx)
{
   /* We need the precise memory layout */
   pan_varying_layout_require_layout(ctx->varying_layout);
   const struct pan_varying_slot *slot =
      pan_varying_layout_slot_at(ctx->varying_layout, idx);
   assert(slot);

   nir_def *index = nir_load_idvs_output_buf_index_pan(b);

   uint32_t res, view_stride;
   if (slot->section == PAN_VARYING_SECTION_GENERIC) {
      /* The varying buffer is bound at index 1 on v12+ */
      uint32_t res_index = ctx->arch >= 12 ? 1 : 0;
      res = pan_res_handle(61, res_index);
      view_stride = 4;
   } else {
      res = pan_res_handle(61, 0);
      view_stride = ctx->has_extended_fifo ? 8 : 4;
   }

   uint32_t index_offset = view_index * view_stride;
   if (slot->section == PAN_VARYING_SECTION_ATTRIBS)
      index_offset += 4;

   /* v9+ cache hints, generic varyings don't need caching while
    * position/attribute varyings are reused by other units inside of the GPU.
    * TODO: Do we really want ESTREAM on generic varyings?
    */
   enum gl_access_qualifier access =
      slot->section == PAN_VARYING_SECTION_GENERIC ? ACCESS_ESTREAM_PAN :
                                                     ACCESS_ISTREAM_PAN;

   index = nir_iadd_imm(b, index, index_offset);
   nir_def *addr = nir_lea_buf_pan(b, nir_imm_int(b, res), index);
   addr = nir_pack_64_2x32(b, addr);
   addr = nir_iadd(b, addr, nir_imm_int64(b, slot->offset));

   /* Tag writes to gl_PointSize with a special intrinsic */
   if (slot->location == VARYING_SLOT_PSIZ) {
      nir_store_global_psiz_pan(b, data, addr, .access = access);
   } else {
      nir_store_global(b, data, addr, .access = access);
   }
}

static void
build_attr_desc_write(struct nir_builder *b, nir_def *data, uint32_t base,
                      nir_alu_type src_type,
                      const struct lower_vs_outputs_ctx *ctx)
{
   nir_def *index = nir_imm_int(b, base);
   nir_def *vertex_id = nir_load_raw_vertex_id_pan(b);
   nir_def *instance_id = nir_load_instance_id(b);

   nir_def *addr_cvt = nir_lea_attr_pan(b, index, vertex_id, instance_id,
                                        .src_type = src_type);
   nir_def *addr = nir_pack_64_2x32(b, nir_trim_vector(b, addr_cvt, 2));
   nir_def *cvt = nir_channel(b, addr_cvt, 2);

   nir_store_global_cvt_pan(b, data, addr, cvt, .src_type = src_type);
}

static void
build_store_output(struct nir_builder *b, nir_def *data, uint32_t idx,
                   nir_alu_type src_type,
                   const struct lower_vs_outputs_ctx *ctx)
{
   pan_varying_layout_require_format(ctx->varying_layout);

   const struct pan_varying_slot *slot =
      pan_varying_layout_slot_at(ctx->varying_layout, idx);
   assert(slot);

   nir_io_semantics sem = {
      .location = slot->location,
      .num_slots = 1,
   };

   nir_store_output(b, data, nir_imm_int(b, 0), .base = idx, .range = 1,
                    .write_mask = BITFIELD_MASK(slot->ncomps), .component = 0,
                    .src_type = src_type, .io_semantics = sem);
}

static void
lower_vs_output_store(struct nir_builder *b,
                      unsigned view_index,
                      unsigned idx, const struct lower_vs_outputs_ctx *ctx)
{
   nir_variable *var = ctx->variables[idx];
   assert(var != NULL);

   nir_alu_type src_type =
      nir_get_nir_type_for_glsl_type(glsl_without_array(var->type));

   bool is_per_view = glsl_type_is_array(var->type);
   assert(is_per_view || view_index == 0);
   nir_def *data = is_per_view ? nir_load_array_var_imm(b, var, view_index)
                               : nir_load_var(b, var);

   if (ctx->arch >= 9 && ctx->has_idvs) {
      build_attr_buf_write(b, data, idx, view_index, ctx);
   } else if (ctx->arch >= 6) {
      assert(!is_per_view);
      build_attr_desc_write(b, data, idx, src_type, ctx);
   } else {
      assert(!is_per_view);
      build_store_output(b, data, idx, src_type, ctx);
   }
}

static nir_variable *
get_or_create_var(nir_builder *b, struct lower_vs_outputs_ctx *ctx,
                  nir_intrinsic_instr *intr)
{
   assert(intr->intrinsic == nir_intrinsic_store_output ||
          intr->intrinsic == nir_intrinsic_store_per_view_output);
   bool is_per_view = intr->intrinsic == nir_intrinsic_store_per_view_output;
   ASSERTED nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   unsigned slot_idx = nir_intrinsic_base(intr);
   nir_alu_type src_type = nir_intrinsic_src_type(intr);
   enum glsl_base_type base_type = nir_get_glsl_base_type_for_nir_type(src_type);

   /* Indirect array varyings are not yet supported (num_slots > 1) */
   assert(sem.num_slots == 1);
   assert(nir_src_as_uint(*nir_get_io_offset_src(intr)) == 0);

   nir_variable *var = ctx->variables[slot_idx];
   if (var != NULL) {
      /* All stores should agree per-location */
      assert(glsl_type_is_array(var->type) == is_per_view);
      assert(glsl_get_base_type(glsl_without_array(var->type)) == base_type);
      return var;
   }

   /* We need the slot section for the number of components */
   pan_varying_layout_require_format(ctx->varying_layout);
   const struct pan_varying_slot *slot =
      pan_varying_layout_slot_at(ctx->varying_layout, slot_idx);
   /* Special slots are read only */
   assert(slot && slot->section != PAN_VARYING_SECTION_SPECIAL &&
          slot->location == sem.location);

   const glsl_type *var_type = glsl_vector_type(base_type, slot->ncomps);
   if (is_per_view)
      var_type = glsl_array_type(var_type, PAN_MAX_MULTIVIEW_VIEW_COUNT, false);

   var = nir_local_variable_create(b->impl, var_type, "vs_out_tmp");
   ctx->variables[slot_idx] = var;
   return var;
}

static bool
gather_vs_outputs(struct nir_builder *b,
                  nir_intrinsic_instr *intr, void *cb_data)
{
   struct lower_vs_outputs_ctx *ctx = cb_data;

   if (intr->intrinsic != nir_intrinsic_store_output &&
       intr->intrinsic != nir_intrinsic_store_per_view_output)
      return false;

   unsigned mask = nir_intrinsic_write_mask(intr);
   unsigned slot_idx = nir_intrinsic_base(intr);
   unsigned component = nir_intrinsic_component(intr);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   bool is_per_view = intr->intrinsic == nir_intrinsic_store_per_view_output;
   unsigned view_index = is_per_view ? nir_src_as_uint(intr->src[1]) : 0;

   ctx->per_view_written[slot_idx] |= BITFIELD_BIT(view_index);
   ctx->used_buckets |= BITFIELD_BIT(va_shader_output_from_loc(sem.location));
   ctx->uses_multiview |= is_per_view;

   b->cursor = nir_instr_remove(&intr->instr);
   nir_variable *var = get_or_create_var(b, ctx, intr);

   unsigned ncomps = glsl_get_vector_elements(glsl_without_array(var->type));

   /* store_output semantics differ from store_var when we write a subset of
    * the components, we need to re-swizzle the channels manually.
    */
   nir_def *stored = intr->src[0].ssa;
   if (stored->num_components != ncomps) {
      nir_def *undef = nir_undef(b, 1, stored->bit_size);
      assert(ncomps <= 4);
      nir_def *channels[4] = {undef, undef, undef, undef};

      for (unsigned i = 0; i < stored->num_components; i++) {
         assert(component + i < 4);
         channels[component + i] = nir_channel(b, stored, i);
      }

      stored = nir_vec(b, channels, ncomps);
      mask = mask << component; /* mask was relative to intr->component */
   } else {
      assert(component == 0);
   }

   if (is_per_view)
      nir_store_array_var_imm(b, var, view_index, stored, mask);
   else
      nir_store_var(b, var, stored, mask);

   return true;
}

bool
pan_nir_lower_vs_outputs(nir_shader *shader, uint64_t gpu_id,
                         const struct pan_varying_layout *varying_layout,
                         bool has_idvs, bool *needs_extended_fifo)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   struct lower_vs_outputs_ctx ctx = {
      .arch = pan_arch(gpu_id),
      .varying_layout = varying_layout,
      .has_idvs = has_idvs,
      .has_extended_fifo = false, /* computed after gather_vs_outputs */
      .variables = {NULL, },
      .per_view_written = {0, },
      .used_buckets = 0,
      .uses_multiview = false,
   };
   /* We use uint8 as a viewcount bitmask */
   assert(PAN_MAX_MULTIVIEW_VIEW_COUNT <= 8 * sizeof(ctx.per_view_written[0]));
   bool progress = nir_shader_intrinsics_pass(shader, gather_vs_outputs,
                                              nir_metadata_control_flow,
                                              (void *)&ctx);
   if (!progress)
      return false;

   /* Should we use extended FIFO? */
   if (ctx.arch >= 9) {
      assert(needs_extended_fifo);
      const uint64_t outputs = shader->info.outputs_written;
      ctx.has_extended_fifo =
         valhal_writes_extended_fifo(outputs, false, ctx.uses_multiview);
      /* Export if we need ex_fifo even without psiz.  The backend needs to
       * know this because we can patch psiz out.
       */
      *needs_extended_fifo =
         valhal_writes_extended_fifo(outputs, true, ctx.uses_multiview);
   }

   nir_builder builder = nir_builder_at(nir_after_impl(impl));
   nir_builder *b = &builder;
   nir_def *shader_output = has_idvs ? nir_load_shader_output_pan(b) : NULL;

   for (int out = 0; out < VA_SHADER_OUTPUT_COUNT; out++) {
      /* Avoid looping a lot and adding ifs (in case of IDVS) */
      if (!(ctx.used_buckets & BITFIELD_BIT(out)))
         continue;

      nir_if *nif = NULL;
      if (has_idvs) {
         nir_def *should_write =
            nir_i2b(b, nir_iand_imm(b, shader_output, BITFIELD_BIT(out)));
         nif = nir_push_if(b, should_write);
      }

      for (unsigned idx = 0; idx < varying_layout->count; idx++) {
         nir_variable *var = ctx.variables[idx];
         if (var == NULL)
            continue;

         const struct pan_varying_slot *slot =
            pan_varying_layout_slot_at(varying_layout, idx);
         if (slot == NULL || va_shader_output_from_loc(slot->location) != out)
            continue;

         /* Non-multiview stores are treated as writing just view index 0 */
         unsigned view_mask = ctx.per_view_written[idx];
         assert(view_mask != 0);

         u_foreach_bit(view_index, view_mask)
            lower_vs_output_store(b, view_index, idx, &ctx);
      }

      if (nif)
         nir_pop_if(b, nif);
   }

   return nir_progress(true, impl, nir_metadata_none);
}
