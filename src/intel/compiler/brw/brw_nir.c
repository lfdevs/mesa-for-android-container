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

#include "intel_nir.h"
#include "brw_nir.h"
#include "brw_private.h"
#include "brw_sampler.h"
#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"
#include "dev/intel_debug.h"
#include "util/sparse_bitset.h"

/**
 * Returns the minimum number of vec4 elements needed to pack a type.
 *
 * For simple types, it will return 1 (a single vec4); for matrices, the
 * number of columns; for array and struct, the sum of the vec4_size of
 * each of its elements; and for sampler and atomic, zero.
 *
 * This method is useful to calculate how much register space is needed to
 * store a particular type.
 */
int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static bool
is_input(nir_intrinsic_instr *intrin)
{
   return intrin->intrinsic == nir_intrinsic_load_input ||
          intrin->intrinsic == nir_intrinsic_load_per_primitive_input ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_input ||
          intrin->intrinsic == nir_intrinsic_load_interpolated_input;
}

static bool
is_output(nir_intrinsic_instr *intrin)
{
   return intrin->intrinsic == nir_intrinsic_load_output ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_load_per_view_output ||
          intrin->intrinsic == nir_intrinsic_store_output ||
          intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_store_per_view_output;
}

static unsigned
io_component(nir_intrinsic_instr *instr)
{
   if (nir_intrinsic_has_component(instr))
      return nir_intrinsic_component(instr);
   else
      return 0;
}

static nir_def *
load_urb(nir_builder *b,
         const struct intel_device_info *devinfo,
         nir_intrinsic_instr *intrin,
         nir_def *handle,
         enum gl_access_qualifier access)
{
   nir_def *offset = nir_get_io_offset_src(intrin)->ssa;

   const unsigned base = nir_intrinsic_base(intrin);
   const unsigned bits = intrin->def.bit_size;

   if (devinfo->ver >= 20) {
      nir_def *addr = nir_iadd(b, handle, nir_ishl_imm(b, offset, 4));
      return nir_load_urb_lsc_intel(b, intrin->def.num_components, bits, addr,
                                    16 * base + 4 * io_component(intrin));
   }

   /* Load a whole vec4 and return the desired portion */
   const unsigned first_component = io_component(intrin);
   const unsigned components = intrin->def.num_components + first_component;
   assert(components <= 4);

   nir_def *load =
      nir_load_urb_vec4_intel(b, components, bits, handle, offset,
                              .base = base, .access = access);
   nir_component_mask_t mask =
      nir_component_mask(intrin->def.num_components) << first_component;

   return nir_channels(b, load, mask);
}

static void
store_urb(nir_builder *b,
          const struct intel_device_info *devinfo,
          nir_intrinsic_instr *intrin,
          nir_def *urb_handle)
{
   nir_def *src = intrin->src[0].ssa;
   nir_def *offset = nir_get_io_offset_src(intrin)->ssa;

   unsigned mask = nir_intrinsic_write_mask(intrin);

   if (devinfo->ver >= 20) {
      nir_def *addr = nir_iadd(b, urb_handle, nir_ishl_imm(b, offset, 4));
      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);

         const unsigned cur_mask = BITFIELD_MASK(count) << start;
         const unsigned base = 16 * nir_intrinsic_base(intrin) +
                               4 * (start + io_component(intrin));

         nir_store_urb_lsc_intel(b, nir_channels(b, src, cur_mask), addr,
                                 .base = base);
      }
   } else {
      const unsigned first_component = io_component(intrin);
      if (first_component) {
         const unsigned components = src->num_components + first_component;
         assert(components <= 4);

         mask <<= first_component;
         src = nir_shift_channels(b, src, first_component, components);
      }
      nir_store_urb_vec4_intel(b, src, urb_handle, offset,
                               nir_imm_int(b, mask),
                               .base = nir_intrinsic_base(intrin));
   }
}

static nir_def *
input_handle(nir_builder *b, nir_intrinsic_instr *intrin)
{
   const enum mesa_shader_stage stage = b->shader->info.stage;
   nir_src *vertex = nir_get_io_arrayed_index_src(intrin);

   return stage == MESA_SHADER_TESS_CTRL || stage == MESA_SHADER_GEOMETRY ?
          nir_load_urb_input_handle_indexed_intel(b, 1, 32, vertex->ssa) :
          nir_load_urb_input_handle_intel(b);
}

static nir_def *
output_handle(nir_builder *b)
{
   return nir_load_urb_output_handle_intel(b);
}

static nir_def *
load_push_input(nir_builder *b, nir_intrinsic_instr *io, unsigned byte_offset)
{
   return nir_load_attribute_payload_intel(b, io->def.num_components,
                                           io->def.bit_size,
                                           nir_imm_int(b, byte_offset));
}

static nir_def *
try_load_push_input(nir_builder *b,
                    const struct intel_device_info *devinfo,
                    nir_intrinsic_instr *io)
{
   nir_src *offset = nir_get_io_offset_src(io);
   if (!nir_src_is_const(*offset))
      return NULL;

   /* nir_io_add_const_offset_to_base guarantees this */
   assert(nir_src_as_uint(*offset) == 0);

   const uint32_t base = nir_intrinsic_base(io);
   const uint32_t byte_offset = 16 * base + 4 * io_component(io);
   assert((byte_offset % 4) == 0);

   const enum mesa_shader_stage stage = b->shader->info.stage;
   static const unsigned max_push_bytes[MESA_SHADER_MESH + 1] = {
      [MESA_SHADER_TESS_EVAL] = 32 * 16 /* 32 vec4s */
   };

   if (byte_offset >= max_push_bytes[stage])
      return NULL;

   return load_push_input(b, io, byte_offset);
}

static bool
lower_urb_inputs(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   const struct intel_device_info *devinfo = data;

   if (intrin->intrinsic == nir_intrinsic_load_input ||
       intrin->intrinsic == nir_intrinsic_load_per_vertex_input) {
      b->cursor = nir_before_instr(&intrin->instr);

      nir_def *load = try_load_push_input(b, devinfo, intrin);
      if (!load) {
         load = load_urb(b, devinfo, intrin, input_handle(b, intrin),
                         ACCESS_CAN_REORDER | ACCESS_NON_WRITEABLE);
      }
      nir_def_replace(&intrin->def, load);
      return true;
   }
   return false;
}

static bool
lower_urb_outputs(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   const struct intel_device_info *devinfo = data;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *load = NULL;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_output:
      load = load_urb(b, devinfo, intrin, output_handle(b), 0);
      break;
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
      store_urb(b, devinfo, intrin, output_handle(b));
      break;
   case nir_intrinsic_load_per_view_output:
   case nir_intrinsic_store_per_view_output:
      UNREACHABLE("should have been lowered");
   default:
      return false;
   }

   if (load)
      nir_def_replace(&intrin->def, load);
   else
      nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_inputs_to_urb_intrinsics(nir_shader *nir,
                               const struct intel_device_info *devinfo)
{
   return nir_shader_intrinsics_pass(nir, lower_urb_inputs,
                                     nir_metadata_control_flow,
                                     (void *) devinfo);
}

static bool
lower_outputs_to_urb_intrinsics(nir_shader *nir,
                                const struct intel_device_info *devinfo)
{
   return nir_shader_intrinsics_pass(nir, lower_urb_outputs,
                                     nir_metadata_control_flow,
                                     (void *) devinfo);
}

static bool
remap_tess_levels_legacy(nir_builder *b,
                         nir_intrinsic_instr *intrin,
                         void *data)
{
   /* Note that this pass does not work with Xe2 LSC URB messages, but
    * we never use legacy layouts there anyway.
    */
   enum tess_primitive_mode prim = (uintptr_t) data;

   if (!(b->shader->info.stage == MESA_SHADER_TESS_CTRL && is_output(intrin)) &&
       !(b->shader->info.stage == MESA_SHADER_TESS_EVAL && is_input(intrin)))
      return false;

   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   if (io_sem.location != VARYING_SLOT_TESS_LEVEL_INNER &&
       io_sem.location != VARYING_SLOT_TESS_LEVEL_OUTER)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   const bool inner = io_sem.location == VARYING_SLOT_TESS_LEVEL_INNER;

   nir_def *tess_config = nir_load_tess_config_intel(b);
   nir_def *is_tri =
      prim == TESS_PRIMITIVE_UNSPECIFIED ?
         nir_test_mask(b, tess_config, INTEL_TESS_CONFIG_TRIANGLES) :
         nir_imm_bool(b, prim == TESS_PRIMITIVE_TRIANGLES);
   nir_def *is_isoline =
      prim == TESS_PRIMITIVE_UNSPECIFIED ?
         nir_test_mask(b, tess_config, INTEL_TESS_CONFIG_ISOLINES) :
         nir_imm_bool(b, prim == TESS_PRIMITIVE_ISOLINES);

   /* The patch layout is described in the SKL PRMs, Volume 7: 3D-Media-GPGPU,
    * Patch URB Entry (Patch Record) Output, Patch Header DW0-7.  In the chart
    * below TessLevelInner = <ix, iy> and TessLevelOuter = <x, y, z, w>:
    *
    *    [ 7  6  5  4  |  3  2  1  0]
    *
    *    [ x  y  z  w  | ix iy __ __] quad legacy
    *    [ x  y  z ix  | __ __ __ __] tri legacy
    *    [ y  x __ __  | __ __ __ __] isoline legacy
    *
    * From this, we can see:
    * - Outer lives at slot 1
    * - Inner lives at slot 0 for quads but slot 1 for triangles
    * - Inner does not exist for isolines
    * - Isolines need the original value but mask << 2
    * - Triangles+Inner need the original value and mask
    * - Quads or Triangles+Outer need the value and mask flipped (WYZX)
    */
   if (intrin->intrinsic == nir_intrinsic_load_input) {
      /* The TES is guaranteed to know the primitive mode and we always
       * push the first two input slots.
       */
      assert(b->shader->info.stage == MESA_SHADER_TESS_EVAL);
      assert(prim != TESS_PRIMITIVE_UNSPECIFIED);

      nir_def *result;
      if (inner && prim == TESS_PRIMITIVE_TRIANGLES) {
         result = load_push_input(b, intrin, 4 * sizeof(uint32_t));
      } else if (prim == TESS_PRIMITIVE_ISOLINES) {
         result = load_push_input(b, intrin, 6 * sizeof(uint32_t));
      } else {
         const unsigned start =
            (inner ? 4 : 8) - nir_intrinsic_component(intrin)
                            - intrin->def.num_components;

         nir_def *tmp = load_push_input(b, intrin, start * sizeof(uint32_t));

         unsigned reverse[NIR_MAX_VEC_COMPONENTS];
         for (unsigned i = 0; i < tmp->num_components; ++i)
            reverse[i] = tmp->num_components - 1 - i;

         result = nir_swizzle(b, tmp, reverse, tmp->num_components);
      }
      nir_def_replace(&intrin->def, result);
   } else {
      assert(b->shader->info.stage == MESA_SHADER_TESS_CTRL);
      const unsigned wzyx[4] = { 3, 2, 1, 0 };
      const unsigned xxxy[4] = { 0, 0, 0, 1 };
      const unsigned zwww[4] = { 2, 3, 3, 3 };

      nir_def *slot = inner ? nir_b2i32(b, is_tri) : nir_imm_int(b, 1);

      if (intrin->intrinsic == nir_intrinsic_store_output) {
         const unsigned mask = nir_intrinsic_write_mask(intrin);
         const unsigned revmask =
            !!(mask & WRITEMASK_X) << 3 | !!(mask & WRITEMASK_Y) << 2 |
            !!(mask & WRITEMASK_Z) << 1 | !!(mask & WRITEMASK_W) << 0;

         nir_def *padded = nir_pad_vector_imm_int(b, intrin->src[0].ssa, 0, 4);

         nir_def *new_val =
            inner ? nir_bcsel(b, is_tri, nir_channel(b, padded, 0),
                                         nir_swizzle(b, padded, wzyx, 4))
                  : nir_bcsel(b, is_isoline, nir_swizzle(b, padded, xxxy, 4),
                                             nir_swizzle(b, padded, wzyx, 4));

         nir_def *new_mask =
            inner ? nir_bcsel(b, is_tri, nir_imm_int(b, mask & WRITEMASK_X),
                                         nir_imm_int(b, revmask))
                  : nir_bcsel(b, is_isoline, nir_imm_int(b, mask << 2),
                    nir_bcsel(b, is_tri,     nir_imm_int(b, revmask & WRITEMASK_YZW),
                                             nir_imm_int(b, revmask)));

         nir_store_urb_vec4_intel(b, new_val, output_handle(b),
                                  slot, new_mask);
         nir_instr_remove(&intrin->instr);
      } else {
         assert(intrin->intrinsic == nir_intrinsic_load_output);
         nir_def *vec =
            nir_load_urb_vec4_intel(b, 4, 32, output_handle(b), slot);
         const unsigned nc = intrin->def.num_components;

         nir_def *result =
            inner ? nir_bcsel(b, is_tri, nir_trim_vector(b, vec, nc),
                                         nir_swizzle(b, vec, wzyx, nc))
                  : nir_bcsel(b, is_isoline, nir_swizzle(b, vec, zwww, nc),
                                             nir_swizzle(b, vec, wzyx, nc));

         nir_def_replace(&intrin->def, result);
      }
   }

   return true;
}

struct remap_tesslevel_cb_data {
   const struct intel_device_info *devinfo;
   enum tess_primitive_mode prim_mode;
};

static bool
remap_tess_levels_reversed(nir_builder *b,
                           nir_intrinsic_instr *io,
                           void *data)
{
   const struct remap_tesslevel_cb_data *cb_data = data;
   const struct intel_device_info *devinfo = cb_data->devinfo;

   /* The Gfx12+ reversed patch header layouts are:
    *
    *    [ 7  6  5  4  |  3  2  1  0]
    *    [__ __ iy ix  |  w  z  y  x] quad reversed
    *    [__ __ __ __  | ix  z  y  x] tri reversed
    *    [__ __ __ ix  | __  z  y  x] tri reversed inside separate
    *    [__ __ __ __  | __ __  x  y] isoline reversed
    *
    * By using the separate layout for triangles, no remapping is required
    * except that isolines is backwards for some reason.  We flip it here.
    */

   if (!nir_intrinsic_has_io_semantics(io) ||
       nir_intrinsic_io_semantics(io).location !=
       VARYING_SLOT_TESS_LEVEL_OUTER)
      return false;

   b->cursor = nir_after_instr(&io->instr);

   nir_def *is_isoline;
   if (cb_data->prim_mode == TESS_PRIMITIVE_UNSPECIFIED) {
      nir_def *tess_config = nir_load_tess_config_intel(b);
      is_isoline = nir_test_mask(b, tess_config, INTEL_TESS_CONFIG_ISOLINES);
   } else {
      is_isoline = nir_imm_true(b);
   }

   const unsigned yx[2] = { 1, 0 };

   if (io->intrinsic == nir_intrinsic_store_output) {
      /* Flip isolines source: xy__ -> yx__ */
      const unsigned mask = nir_intrinsic_write_mask(io);
      const unsigned revmask = (mask & ~WRITEMASK_XY) |
         (mask & WRITEMASK_X) << 1 | (mask & WRITEMASK_Y) >> 1;

      nir_def *new_val =
         nir_bcsel(b, is_isoline,
                   nir_pad_vector(b, nir_swizzle(b, io->src[0].ssa, yx, 2),
                                  nir_src_num_components(io->src[0])),
                   io->src[0].ssa);

      if (devinfo->ver >= 20) {
         nir_store_urb_lsc_intel(b, new_val, output_handle(b),
                                 .base = mask == WRITEMASK_X ? 4 : 0);
      } else {
         nir_store_urb_vec4_intel(b, new_val, output_handle(b),
                                  nir_imm_int(b, 0),
                                  nir_bcsel(b, is_isoline,
                                            nir_imm_int(b, revmask),
                                            nir_imm_int(b, mask)));
      }
      nir_instr_remove(&io->instr);
   } else {
      /* Just leave these as load intrinsics and let the generic remapper
       * take care of that part.
       */
      nir_def *new_val =
         nir_bcsel(b, is_isoline, nir_swizzle(b, &io->def, yx, 2), &io->def);
      nir_def_rewrite_uses_after(&io->def, new_val);
   }

   return true;
}

static bool
remap_tess_levels(nir_shader *nir,
                  const struct intel_device_info *devinfo,
                  enum tess_primitive_mode prim)
{
   /* Pre-Gfx12 use legacy patch header layouts */
   if (devinfo->ver < 12) {
      return nir_shader_intrinsics_pass(nir, remap_tess_levels_legacy,
                                        nir_metadata_control_flow,
                                        (void *)(uintptr_t) prim);
   }

   /* With the reversed layouts, remapping is only required for
    * isolines (or unspecified, which might be isolines).
    */
   if (prim != TESS_PRIMITIVE_ISOLINES && prim != TESS_PRIMITIVE_UNSPECIFIED)
      return false;

   struct remap_tesslevel_cb_data cb = {
      .devinfo = devinfo, .prim_mode = prim
   };
   return nir_shader_intrinsics_pass(nir, remap_tess_levels_reversed,
                                     nir_metadata_control_flow, &cb);
}

static bool
remap_patch_urb_offsets_instr(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   const struct intel_vue_map *vue_map = data;

   if (!(b->shader->info.stage == MESA_SHADER_TESS_CTRL && is_output(intrin)) &&
       !(b->shader->info.stage == MESA_SHADER_TESS_EVAL && is_input(intrin)))
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   gl_varying_slot varying = io_sem.location;

   int vue_slot = vue_map->varying_to_slot[varying];
   assert(vue_slot != -1);
   nir_intrinsic_set_base(intrin, vue_slot);

   nir_src *vertex = nir_get_io_arrayed_index_src(intrin);
   if (vertex) {
      b->cursor = nir_before_instr(&intrin->instr);

      bool dyn_tess_config =
         b->shader->info.stage == MESA_SHADER_TESS_EVAL &&
         vue_map->layout != INTEL_VUE_LAYOUT_FIXED;
      nir_def *num_per_vertex_slots =
         dyn_tess_config ? intel_nir_tess_field(b, PER_VERTEX_SLOTS) :
         nir_imm_int(b, vue_map->num_per_vertex_slots);

      /* Multiply by the number of per-vertex slots. */
      nir_def *vertex_offset = nir_imul(b, vertex->ssa, num_per_vertex_slots);

      /* Add it to the existing offset */
      nir_src *offset = nir_get_io_offset_src(intrin);
      nir_def *total_offset = nir_iadd(b, vertex_offset, offset->ssa);

      /* In the Tessellation evaluation shader, reposition the offset of
       * builtins when using separate layout.
       */
      if (dyn_tess_config) {
         if (varying < VARYING_SLOT_VAR0) {
            nir_def *builtins_offset = intel_nir_tess_field(b, BUILTINS);
            nir_def *builtins_base_offset = nir_iadd_imm(
               b, builtins_offset,
               vue_map->varying_to_slot[varying] - vue_map->builtins_slot_offset);

            total_offset = nir_iadd(b, total_offset, builtins_base_offset);
         } else {
            nir_def *vertices_offset = intel_nir_tess_field(b, PER_PATCH_SLOTS);
            nir_def *vertices_base_offset = nir_iadd_imm(
               b, vertices_offset,
               vue_map->varying_to_slot[varying] - vue_map->num_per_patch_slots);

            total_offset = nir_iadd(b, total_offset, vertices_base_offset);
         }
         nir_intrinsic_set_base(intrin, 0);
      }

      nir_src_rewrite(offset, total_offset);

      /* Putting an address into offset_src requires that NIR validation of
       * IO intrinsics is disabled.
       */
      io_sem.no_validate = 1;
      nir_intrinsic_set_io_semantics(intrin, io_sem);
   }

   return true;
}

static bool
remap_patch_urb_offsets(nir_shader *nir,
                        const struct intel_vue_map *vue_map)
{
   return nir_shader_intrinsics_pass(nir, remap_patch_urb_offsets_instr,
                                     nir_metadata_control_flow, (void *)vue_map);
}

/* Replace store_per_view_output to plain store_output, mapping the view index
 * to IO offset. Because we only use per-view outputs for position, the offset
 * pitch is always 1. */
static bool
lower_per_view_outputs(nir_builder *b,
                       nir_intrinsic_instr *intrin,
                       UNUSED void *cb_data)
{
   if (intrin->intrinsic != nir_intrinsic_store_per_view_output &&
       intrin->intrinsic != nir_intrinsic_load_per_view_output)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_src *view_index = nir_get_io_arrayed_index_src(intrin);
   nir_src *offset = nir_get_io_offset_src(intrin);

   nir_def *new_offset = nir_iadd(b, view_index->ssa, offset->ssa);

   nir_intrinsic_instr *new;
   if (intrin->intrinsic == nir_intrinsic_store_per_view_output)
      new = nir_store_output(b, intrin->src[0].ssa, new_offset);
   else {
      nir_def *new_def = nir_load_output(b, intrin->def.num_components,
                                         intrin->def.bit_size, new_offset);
      new = nir_def_as_intrinsic(new_def);
   }

   nir_intrinsic_set_base(new, nir_intrinsic_base(intrin));
   nir_intrinsic_set_range(new, nir_intrinsic_range(intrin));
   nir_intrinsic_set_write_mask(new, nir_intrinsic_write_mask(intrin));
   nir_intrinsic_set_component(new, nir_intrinsic_component(intrin));
   nir_intrinsic_set_src_type(new, nir_intrinsic_src_type(intrin));

   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
   /* the meaning of the offset src is different for brw */
   sem.no_validate = 1;
   nir_intrinsic_set_io_semantics(new, sem);

   if (intrin->intrinsic == nir_intrinsic_load_per_view_output)
      nir_def_rewrite_uses(&intrin->def, &new->def);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
brw_nir_lower_per_view_outputs(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_per_view_outputs,
                                     nir_metadata_control_flow,
                                     NULL);
}

void
brw_nir_lower_vs_inputs(nir_shader *nir)
{
   /* Start with the location of the variable's base. */
   nir_foreach_shader_in_variable(var, nir)
      var->data.driver_location = var->data.location;

   /* Now use nir_lower_io to walk dereference chains.  Attribute arrays are
    * loaded as one vec4 or dvec4 per element (or matrix column), depending on
    * whether it is a double-precision type or not.
    */
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in, type_size_vec4,
            nir_lower_io_lower_64bit_to_32_new);

   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   /* Update shader_info::dual_slot_inputs */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* The last step is to remap VERT_ATTRIB_* to actual registers */

   /* Whether or not we have any system generated values.  gl_DrawID is not
    * included here as it lives in its own vec4.
    */
   const bool has_sgvs =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_FIRST_VERTEX) ||
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE) ||
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE) ||
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);

   const unsigned num_inputs = util_bitcount64(nir->info.inputs_read) +
      util_bitcount64(nir->info.inputs_read & nir->info.dual_slot_inputs);

   /* In the following loop, the intrinsic base value is the offset in
    * register slots (2 slots can make up in single input for double/64bit
    * values). The io_semantics location field is the offset in terms of
    * attributes.
    */

   nir_foreach_function_impl(impl, nir) {
      nir_builder b = nir_builder_create(impl);

      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            switch (intrin->intrinsic) {
            case nir_intrinsic_load_first_vertex:
            case nir_intrinsic_load_base_instance:
            case nir_intrinsic_load_vertex_id_zero_base:
            case nir_intrinsic_load_instance_id:
            case nir_intrinsic_load_is_indexed_draw:
            case nir_intrinsic_load_draw_id: {
               b.cursor = nir_after_instr(&intrin->instr);

               /* gl_VertexID and friends are stored by the VF as the last
                * vertex element.  We convert them to load_input intrinsics at
                * the right location.
                */
               nir_intrinsic_instr *load =
                  nir_intrinsic_instr_create(nir, nir_intrinsic_load_input);
               load->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));

               unsigned input_offset = 0;
               unsigned location = BRW_SVGS_VE_INDEX;
               switch (intrin->intrinsic) {
               case nir_intrinsic_load_first_vertex:
                  nir_intrinsic_set_component(load, 0);
                  break;
               case nir_intrinsic_load_base_instance:
                  nir_intrinsic_set_component(load, 1);
                  break;
               case nir_intrinsic_load_vertex_id_zero_base:
                  nir_intrinsic_set_component(load, 2);
                  break;
               case nir_intrinsic_load_instance_id:
                  nir_intrinsic_set_component(load, 3);
                  break;
               case nir_intrinsic_load_draw_id:
               case nir_intrinsic_load_is_indexed_draw:
                  /* gl_DrawID and IsIndexedDraw are stored right after
                   * gl_VertexID and friends if any of them exist.
                   */
                  input_offset += has_sgvs ? 1 : 0;
                  location = BRW_DRAWID_VE_INDEX;
                  if (intrin->intrinsic == nir_intrinsic_load_draw_id)
                     nir_intrinsic_set_component(load, 0);
                  else
                     nir_intrinsic_set_component(load, 1);
                  break;
               default:
                  UNREACHABLE("Invalid system value intrinsic");
               }

               /* Position the value behind the app's inputs, for base we
                * account for the double inputs, for the io_semantics
                * location, it's just the input count.
                */
               nir_intrinsic_set_base(load, num_inputs + input_offset);
               struct nir_io_semantics io = {
                  .location = VERT_ATTRIB_GENERIC0 + location,
                  .num_slots = 1,
               };
               nir_intrinsic_set_io_semantics(load, io);
               load->num_components = 1;
               nir_def_init(&load->instr, &load->def, 1, 32);
               nir_builder_instr_insert(&b, &load->instr);

               nir_def_replace(&intrin->def, &load->def);
               break;
            }

            case nir_intrinsic_load_input: {
               /* Attributes come in a contiguous block, ordered by their
                * gl_vert_attrib value.  That means we can compute the slot
                * number for an attribute by masking out the enabled attributes
                * before it and counting the bits.
                */
               const struct nir_io_semantics io =
                  nir_intrinsic_io_semantics(intrin);
               const int attr = nir_intrinsic_base(intrin);
               const int slot = util_bitcount64(nir->info.inputs_read &
                                                BITFIELD64_MASK(attr)) +
                                util_bitcount64(nir->info.dual_slot_inputs &
                                                BITFIELD64_MASK(attr)) +
                                io.high_dvec2;
               nir_intrinsic_set_base(intrin, slot);
               break;
            }

            default:
               break; /* Nothing to do */
            }
         }
      }
   }
}

void
brw_nir_lower_vue_inputs(nir_shader *nir,
                         const struct intel_vue_map *vue_map)
{
   nir_foreach_shader_in_variable(var, nir)
      var->data.driver_location = var->data.location;

   /* Inputs are stored in vec4 slots, so use type_size_vec4(). */
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in, type_size_vec4,
            nir_lower_io_lower_64bit_to_32);

   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (intrin->intrinsic == nir_intrinsic_load_input ||
                intrin->intrinsic == nir_intrinsic_load_per_vertex_input) {
               /* Offset 0 is the VUE header, which contains
                * VARYING_SLOT_LAYER [.y], VARYING_SLOT_VIEWPORT [.z], and
                * VARYING_SLOT_PSIZ [.w].
                */
               nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
               gl_varying_slot varying = io_sem.location;
               int vue_slot;
               switch (varying) {
               case VARYING_SLOT_PSIZ:
                  nir_intrinsic_set_base(intrin, 0);
                  nir_intrinsic_set_component(intrin, 3);
                  break;

               default:
                  vue_slot = vue_map->varying_to_slot[varying];
                  assert(vue_slot != -1);
                  nir_intrinsic_set_base(intrin, vue_slot);
                  break;
               }
            }
         }
      }
   }
}

void
brw_nir_lower_tes_inputs(nir_shader *nir,
                         const struct intel_device_info *devinfo,
                         const struct intel_vue_map *vue_map)
{
   NIR_PASS(_, nir, nir_lower_tess_level_array_vars_to_vec);

   nir_foreach_shader_in_variable(var, nir)
      var->data.driver_location = var->data.location;

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in, type_size_vec4,
            nir_lower_io_lower_64bit_to_32);

   /* Run nir_opt_constant_folding to allow update base/io_semantic::location
    * for the remapping pass to look into the VUE mapping.
    */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   NIR_PASS(_, nir, remap_tess_levels, devinfo,
            nir->info.tess._primitive_mode);
   NIR_PASS(_, nir, remap_patch_urb_offsets, vue_map);

   /* remap_patch_urb_offsets can add constant math into the shader,
    * just fold it for the backend.
    */
   NIR_PASS(_, nir, nir_opt_algebraic);
   NIR_PASS(_, nir, nir_opt_constant_folding);

   NIR_PASS(_, nir, lower_inputs_to_urb_intrinsics, devinfo);
}

static bool
lower_barycentric_per_sample(nir_builder *b,
                             nir_intrinsic_instr *intrin,
                             UNUSED void *cb_data)
{
   if (intrin->intrinsic != nir_intrinsic_load_barycentric_pixel &&
       intrin->intrinsic != nir_intrinsic_load_barycentric_centroid)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *centroid =
      nir_load_barycentric(b, nir_intrinsic_load_barycentric_sample,
                           nir_intrinsic_interp_mode(intrin));
   nir_def_replace(&intrin->def, centroid);
   return true;
}

/**
 * Convert interpolateAtOffset() offsets from [-0.5, +0.5] floating point
 * offsets to integer [-8, +7] offsets (in units of 1/16th of a pixel).
 *
 * We clamp to +7/16 on the upper end of the range, since +0.5 isn't
 * representable in a S0.4 value; a naive conversion would give us -8/16,
 * which is the opposite of what was intended.
 *
 * This is allowed by GL_ARB_gpu_shader5's quantization rules:
 *
 *    "Not all values of <offset> may be supported; x and y offsets may
 *     be rounded to fixed-point values with the number of fraction bits
 *     given by the implementation-dependent constant
 *     FRAGMENT_INTERPOLATION_OFFSET_BITS."
 */
static bool
lower_barycentric_at_offset(nir_builder *b, nir_intrinsic_instr *intrin,
                            void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_barycentric_at_offset)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   assert(intrin->src[0].ssa);
   nir_def *offset =
      nir_imin(b, nir_imm_int(b, 7),
               nir_f2i32(b, nir_fmul_imm(b, intrin->src[0].ssa, 16)));

   nir_src_rewrite(&intrin->src[0], offset);

   return true;
}

static bool
lower_indirect_primitive_id(nir_builder *b,
                            nir_intrinsic_instr *intrin,
                            void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_per_primitive_input)
      return false;

   if (nir_intrinsic_io_semantics(intrin).location != VARYING_SLOT_PRIMITIVE_ID)
      return false;

   nir_def *indirect_primitive_id = data;
   nir_def_replace(&intrin->def, indirect_primitive_id);

   return true;
}

bool
brw_needs_vertex_attributes_bypass(const nir_shader *shader)
{
   /* Even if there are no actual per-vertex inputs, if the fragment
    * shader uses BaryCoord*, we need to set everything accordingly
    * so the barycentrics don't get reordered.
    */
   if (BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_LINEAR_COORD) ||
       BITSET_TEST(shader->info.system_values_read, SYSTEM_VALUE_BARYCENTRIC_PERSP_COORD))
      return true;

   nir_foreach_shader_in_variable(var, shader) {
      if (var->data.per_vertex)
         return true;
   }

   return false;
}

/* Build the per-vertex offset into the attribute section of the per-vertex
 * thread payload. There is always one GRF of padding in front.
 *
 * The computation is fairly complicated due to the layout of the payload. You
 * can find a description of the layout in brw_compile_fs.cpp
 * brw_assign_urb_setup().
 *
 * Gfx < 20 packs 2 slots per GRF (hence the %/ 2 in the formula)
 * Gfx >= 20 pack 5 slots per GRF (hence the %/ 5 in the formula)
 *
 * Then an additional offset needs to added to handle how multiple polygon
 * data is interleaved.
 */
nir_def *
brw_nir_vertex_attribute_offset(nir_builder *b,
                                nir_def *attr_idx,
                                const struct intel_device_info *devinfo)
{
   nir_def *max_poly = nir_load_max_polygon_intel(b);
   return devinfo->ver >= 20 ?
         nir_iadd(b,
                  nir_imul(b, nir_udiv_imm(b, attr_idx, 5), nir_imul_imm(b, max_poly, 64)),
                  nir_imul_imm(b, nir_umod_imm(b, attr_idx, 5), 12)) :
      nir_iadd_imm(
         b,
         nir_iadd(
            b,
            nir_imul(b, nir_udiv_imm(b, attr_idx, 2), nir_imul_imm(b, max_poly, 32)),
            nir_imul_imm(b, nir_umod_imm(b, attr_idx, 2), 16)),
         12);
}

static nir_block *
fragment_top_block_or_after_wa_18019110168(nir_function_impl *impl)
{
   nir_if *first_if =
      nir_block_get_following_if(nir_start_block(impl));
   nir_block *post_wa_18019110168_block = NULL;
   if (first_if) {
      nir_block *last_if_block = nir_if_last_then_block(first_if);
      nir_foreach_block_in_cf_node(block, &first_if->cf_node) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_store_per_primitive_payload_intel) {
               post_wa_18019110168_block = last_if_block->successors[0];
               break;
            }
         }

         if (post_wa_18019110168_block)
            break;
      }
   }

   return post_wa_18019110168_block ?
      post_wa_18019110168_block : nir_start_block(impl);
}

void
brw_nir_lower_fs_inputs(nir_shader *nir,
                        const struct intel_device_info *devinfo,
                        const struct brw_wm_prog_key *key)
{
   /* Always pull the PrimitiveID from the per-primitive block if mesh can be
    * involved.
    */
   if (key->mesh_input != INTEL_NEVER) {
      nir_foreach_shader_in_variable(var, nir) {
         if (var->data.location == VARYING_SLOT_PRIMITIVE_ID) {
            var->data.per_primitive = true;
            nir->info.per_primitive_inputs |= VARYING_BIT_PRIMITIVE_ID;
         }
      }
   }

   nir_def *indirect_primitive_id = NULL;
   if (key->base.vue_layout == INTEL_VUE_LAYOUT_SEPARATE_MESH &&
       (nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID)) {
      nir_builder _b = nir_builder_at(
         nir_before_block(
            fragment_top_block_or_after_wa_18019110168(
               nir_shader_get_entrypoint(nir)))), *b = &_b;
      nir_def *index = nir_ubitfield_extract_imm(
         b,
         nir_load_fs_msaa_intel(b),
         INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_OFFSET,
         INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_SIZE);
     nir_def *per_vertex_offset =
         nir_iadd_imm(
            b,
            brw_nir_vertex_attribute_offset(
               b, nir_imul_imm(b, index, 4), devinfo),
            devinfo->grf_size);
      /* When the attribute index is INTEL_MSAA_FLAG_PRIMITIVE_ID_MESH_INDEX,
       * it means the value is coming from the per-primitive block. We always
       * lay out PrimitiveID at offset 0 in the per-primitive block.
       */
      nir_def *attribute_offset = nir_bcsel(
         b,
         nir_ieq_imm(b, index, INTEL_MSAA_FLAG_PRIMITIVE_ID_INDEX_MESH),
         nir_imm_int(b, 0), per_vertex_offset);
      indirect_primitive_id =
         nir_load_attribute_payload_intel(b, 1, 32, attribute_offset);
   }

   nir_foreach_shader_in_variable(var, nir) {
      var->data.driver_location = var->data.location;

      if (var->data.interpolation == INTERP_MODE_NONE)
         var->data.interpolation = INTERP_MODE_SMOOTH;
   }

   NIR_PASS(_, nir, nir_lower_io,
            nir_var_shader_in, type_size_vec4,
            nir_lower_io_lower_64bit_to_32 |
            nir_lower_io_use_interpolated_input_intrinsics);
   if (devinfo->ver >= 11)
      NIR_PASS(_, nir, nir_lower_interpolation, ~0);

   if (brw_needs_vertex_attributes_bypass(nir))
      brw_nir_lower_fs_barycentrics(nir);

   if (key->multisample_fbo == INTEL_NEVER) {
      NIR_PASS(_, nir, nir_lower_single_sampled);
   } else if (key->persample_interp == INTEL_ALWAYS) {
      NIR_PASS(_, nir, nir_shader_intrinsics_pass,
               lower_barycentric_per_sample,
               nir_metadata_control_flow,
               NULL);
   }

   if (devinfo->ver < 20) {
      NIR_PASS(_, nir, nir_shader_intrinsics_pass,
               lower_barycentric_at_offset,
               nir_metadata_control_flow,
               NULL);
   }

   if (indirect_primitive_id != NULL) {
      NIR_PASS(_, nir, nir_shader_intrinsics_pass,
               lower_indirect_primitive_id,
               nir_metadata_control_flow,
               indirect_primitive_id);
   }

   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, nir, nir_opt_constant_folding);
}

void
brw_nir_lower_vue_outputs(nir_shader *nir)
{
   nir_foreach_shader_out_variable(var, nir) {
      var->data.driver_location = var->data.location;
   }

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out, type_size_vec4,
            nir_lower_io_lower_64bit_to_32);
   NIR_PASS(_, nir, brw_nir_lower_per_view_outputs);
}

void
brw_nir_lower_tcs_inputs(nir_shader *nir,
                         const struct intel_device_info *devinfo,
                         const struct intel_vue_map *input_vue_map)
{
   brw_nir_lower_vue_inputs(nir, input_vue_map);

   NIR_PASS(_, nir, lower_inputs_to_urb_intrinsics, devinfo);
}

void
brw_nir_lower_tcs_outputs(nir_shader *nir,
                          const struct intel_device_info *devinfo,
                          const struct intel_vue_map *vue_map,
                          enum tess_primitive_mode tes_primitive_mode)
{
   NIR_PASS(_, nir, nir_lower_tess_level_array_vars_to_vec);
   NIR_PASS(_, nir, nir_opt_combine_stores, nir_var_shader_out);

   nir_foreach_shader_out_variable(var, nir) {
      var->data.driver_location = var->data.location;
   }

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out, type_size_vec4,
            nir_lower_io_lower_64bit_to_32);

   /* Run nir_opt_constant_folding to allow update base/io_semantic::location
    * for the remapping pass to look into the VUE mapping.
    */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   NIR_PASS(_, nir, remap_tess_levels, devinfo, tes_primitive_mode);
   NIR_PASS(_, nir, remap_patch_urb_offsets, vue_map);

   /* remap_patch_urb_offsets can add constant math into the shader,
    * just fold it for the backend.
    */
   NIR_PASS(_, nir, nir_opt_constant_folding);

   NIR_PASS(_, nir, lower_outputs_to_urb_intrinsics, devinfo);
}

void
brw_nir_lower_fs_outputs(nir_shader *nir)
{
   nir_foreach_shader_out_variable(var, nir) {
      var->data.driver_location =
         SET_FIELD(var->data.index, BRW_NIR_FRAG_OUTPUT_INDEX) |
         SET_FIELD(var->data.location, BRW_NIR_FRAG_OUTPUT_LOCATION);
   }

   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_out, type_size_vec4, 0);
   nir->info.disable_output_offset_src_constant_folding = true;
}

static bool
tag_speculative_access(nir_builder *b,
                       nir_intrinsic_instr *intrin,
                       void *unused)
{
   if (intrin->intrinsic == nir_intrinsic_load_ubo &&
       brw_nir_ubo_surface_index_is_pushable(intrin->src[0])) {
      nir_intrinsic_set_access(intrin, ACCESS_CAN_SPECULATE |
                               nir_intrinsic_access(intrin));
      return true;
   }

   return false;
}

static bool
brw_nir_tag_speculative_access(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, tag_speculative_access,
                                     nir_metadata_all, NULL);
}

#define OPT(pass, ...) ({                                  \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   if (this_progress)                                      \
      progress = true;                                     \
   this_progress;                                          \
})

#define LOOP_OPT(pass, ...) ({                             \
   const unsigned long this_line = __LINE__;               \
   bool this_progress = false;                             \
   if (opt_line == this_line)                              \
      break;                                               \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   if (this_progress) {                                    \
      progress = true;                                     \
      opt_line = this_line;                                \
   }                                                       \
   this_progress;                                          \
})

#define LOOP_OPT_NOT_IDEMPOTENT(pass, ...) ({              \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   if (this_progress) {                                    \
      progress = true;                                     \
      opt_line = 0;                                        \
   }                                                       \
   this_progress;                                          \
})

void
brw_nir_optimize(nir_shader *nir,
                 const struct intel_device_info *devinfo)
{
   bool progress;
   unsigned long opt_line = 0;
   do {
      progress = false;
      /* This pass is causing problems with types used by OpenCL :
       *    https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/13955
       *
       * Running with it disabled made no difference in the resulting assembly
       * code.
       */
      if (nir->info.stage != MESA_SHADER_KERNEL)
         LOOP_OPT(nir_split_array_vars, nir_var_function_temp);
      LOOP_OPT(nir_shrink_vec_array_vars, nir_var_function_temp);
      LOOP_OPT(nir_opt_deref);
      if (LOOP_OPT(nir_opt_memcpy))
         LOOP_OPT(nir_split_var_copies);
      LOOP_OPT(nir_lower_vars_to_ssa);
      if (!nir->info.var_copies_lowered) {
         /* Only run this pass if nir_lower_var_copies was not called
          * yet. That would lower away any copy_deref instructions and we
          * don't want to introduce any more.
          */
         LOOP_OPT(nir_opt_find_array_copies);
      }
      LOOP_OPT(nir_opt_copy_prop_vars);
      LOOP_OPT(nir_opt_dead_write_vars);
      LOOP_OPT(nir_opt_combine_stores, nir_var_all);

      LOOP_OPT(nir_opt_ray_queries);
      LOOP_OPT(nir_opt_ray_query_ranges);

      LOOP_OPT(nir_lower_alu_to_scalar, NULL, NULL);

      LOOP_OPT(nir_opt_copy_prop);

      LOOP_OPT(nir_lower_phis_to_scalar, NULL, NULL);

      LOOP_OPT(nir_opt_copy_prop);
      LOOP_OPT(nir_opt_dce);
      LOOP_OPT(nir_opt_cse);
      LOOP_OPT(nir_opt_combine_stores, nir_var_all);

      /* Passing 0 to the peephole select pass causes it to convert
       * if-statements that contain only move instructions in the branches
       * regardless of the count.
       *
       * Passing 1 to the peephole select pass causes it to convert
       * if-statements that contain at most a single ALU instruction (total)
       * in both branches.  Before Gfx6, some math instructions were
       * prohibitively expensive and the results of compare operations need an
       * extra resolve step.  For these reasons, this pass is more harmful
       * than good on those platforms.
       *
       * For indirect loads of uniforms (push constants), we assume that array
       * indices will nearly always be in bounds and the cost of the load is
       * low.  Therefore there shouldn't be a performance benefit to avoid it.
       */
      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 0,
         .indirect_load_ok = true,
      };
      LOOP_OPT(nir_opt_peephole_select, &peephole_select_options);

      peephole_select_options.limit = 8;
      peephole_select_options.expensive_alu_ok = true;
      LOOP_OPT(nir_opt_peephole_select, &peephole_select_options);

      LOOP_OPT(nir_opt_intrinsics);
      LOOP_OPT(nir_opt_idiv_const, 32);
      LOOP_OPT_NOT_IDEMPOTENT(nir_opt_algebraic);

      LOOP_OPT(nir_opt_generate_bfi);
      LOOP_OPT(nir_opt_reassociate_bfi);

      LOOP_OPT(nir_lower_constant_convert_alu_types);
      LOOP_OPT(nir_opt_constant_folding);

      LOOP_OPT(nir_opt_dead_cf);
      if (LOOP_OPT(nir_opt_loop)) {
         /* If nir_opt_loop makes progress, then we need to clean
          * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
          * to make progress.
          */
         LOOP_OPT(nir_opt_copy_prop);
         LOOP_OPT(nir_opt_dce);
      }
      LOOP_OPT_NOT_IDEMPOTENT(nir_opt_if, nir_opt_if_optimize_phi_true_false);

      nir_opt_peephole_select_options peephole_discard_options = {
         .limit = 0,
         .discard_ok = true,
      };
      LOOP_OPT(nir_opt_peephole_select, &peephole_discard_options);
      if (nir->options->max_unroll_iterations != 0) {
         LOOP_OPT_NOT_IDEMPOTENT(nir_opt_loop_unroll);
      }
      LOOP_OPT(nir_opt_remove_phis);
      LOOP_OPT(nir_opt_gcm, false);
      LOOP_OPT(nir_opt_undef);
      LOOP_OPT(nir_lower_pack);
   } while (progress);

   /* Workaround Gfxbench unused local sampler variable which will trigger an
    * assert in the opt_large_constants pass.
    */
   OPT(nir_remove_dead_variables, nir_var_function_temp, NULL);
}

static unsigned
lower_bit_size_callback(const nir_instr *instr, void *data)
{
   const struct brw_compiler *compiler = data;

   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      switch (alu->op) {
      case nir_op_bit_count:
      case nir_op_ufind_msb:
      case nir_op_ifind_msb:
      case nir_op_find_lsb:
         /* These are handled specially because the destination is always
          * 32-bit and so the bit size of the instruction is given by the
          * source.
          */
         return alu->src[0].src.ssa->bit_size >= 32 ? 0 : 32;
      default:
         break;
      }

      if (alu->def.bit_size >= 32)
         return 0;

      /* Note: nir_op_iabs and nir_op_ineg are not lowered here because the
       * 8-bit ABS or NEG instruction should eventually get copy propagated
       * into the MOV that does the type conversion.  This results in far
       * fewer MOV instructions.
       */
      switch (alu->op) {
      case nir_op_bitfield_reverse:
         return alu->def.bit_size != 32 ? 32 : 0;
      case nir_op_idiv:
      case nir_op_imod:
      case nir_op_irem:
      case nir_op_udiv:
      case nir_op_umod:
         /* Gfx12.5+ lacks integer division instructions. As nir_lower_idiv is
          * far more efficient for int8/int16 divisions, we do not lower here.
          *
          * Older platforms have idiv instructions only for int32, so lower.
          */
         return compiler->devinfo->verx10 >= 125 ? 0 : 32;
      case nir_op_fceil:
      case nir_op_ffloor:
      case nir_op_ffract:
      case nir_op_fround_even:
      case nir_op_ftrunc:
         return 32;
      case nir_op_frcp:
      case nir_op_frsq:
      case nir_op_fsqrt:
      case nir_op_fpow:
      case nir_op_fexp2:
      case nir_op_flog2:
      case nir_op_fsin:
      case nir_op_fcos:
         return 0;
      case nir_op_isign:
         UNREACHABLE("Should have been lowered by nir_opt_algebraic.");
      default:
         if (nir_op_infos[alu->op].num_inputs >= 2 &&
             alu->def.bit_size == 8)
            return 16;

         if (nir_alu_instr_is_comparison(alu) &&
             alu->src[0].src.ssa->bit_size == 8)
            return 16;

         return 0;
      }
      break;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_read_invocation:
      case nir_intrinsic_read_first_invocation:
      case nir_intrinsic_vote_feq:
      case nir_intrinsic_vote_ieq:
      case nir_intrinsic_shuffle:
      case nir_intrinsic_shuffle_xor:
      case nir_intrinsic_shuffle_up:
      case nir_intrinsic_shuffle_down:
      case nir_intrinsic_quad_broadcast:
      case nir_intrinsic_quad_swap_horizontal:
      case nir_intrinsic_quad_swap_vertical:
      case nir_intrinsic_quad_swap_diagonal:
         if (intrin->src[0].ssa->bit_size == 8)
            return 16;
         return 0;

      case nir_intrinsic_reduce:
      case nir_intrinsic_inclusive_scan:
      case nir_intrinsic_exclusive_scan:
         /* There are a couple of register region issues that make things
          * complicated for 8-bit types:
          *
          *    1. Only raw moves are allowed to write to a packed 8-bit
          *       destination.
          *    2. If we use a strided destination, the efficient way to do
          *       scan operations ends up using strides that are too big to
          *       encode in an instruction.
          *
          * To get around these issues, we just do all 8-bit scan operations
          * in 16 bits.  It's actually fewer instructions than what we'd have
          * to do if we were trying to do it in native 8-bit types and the
          * results are the same once we truncate to 8 bits at the end.
          */
         if (intrin->def.bit_size == 8)
            return 16;
         return 0;

      default:
         return 0;
      }
      break;
   }

   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      if (phi->def.bit_size == 8)
         return 16;
      return 0;
   }

   default:
      return 0;
   }
}

/* On gfx12.5+, if the offsets are not both constant and in the {-8,7} range,
 * we will have nir_lower_tex() lower the source offset by returning true from
 * this filter function.
 */
static bool
lower_xehp_tg4_offset_filter(const nir_instr *instr, UNUSED const void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);

   if (tex->op != nir_texop_tg4)
      return false;

   int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_index < 0)
      return false;

   /* When we have LOD & offset, we can pack both (see
    * intel_nir_lower_texture.c pack_lod_or_bias_and_offset)
    */
   bool has_lod =
      nir_tex_instr_src_index(tex, nir_tex_src_lod) != -1 ||
      nir_tex_instr_src_index(tex, nir_tex_src_bias) != -1;
   if (has_lod)
      return false;

   if (!nir_src_is_const(tex->src[offset_index].src))
      return true;

   int64_t offset_x = nir_src_comp_as_int(tex->src[offset_index].src, 0);
   int64_t offset_y = nir_src_comp_as_int(tex->src[offset_index].src, 1);

   return offset_x < -8 || offset_x > 7 || offset_y < -8 || offset_y > 7;
}

/* Does some simple lowering and runs the standard suite of optimizations
 *
 * This is intended to be called more-or-less directly after you get the
 * shader out of GLSL or some other source.  While it is geared towards i965,
 * it is not at all generator-specific.
 */
void
brw_preprocess_nir(const struct brw_compiler *compiler, nir_shader *nir,
                   const struct brw_nir_compiler_opts *opts)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   UNUSED bool progress; /* Written by OPT */

   nir_validate_ssa_dominance(nir, "before brw_preprocess_nir");

   OPT(nir_lower_frexp);

   OPT(nir_lower_alu_to_scalar, NULL, NULL);

   if (nir->info.stage == MESA_SHADER_GEOMETRY)
      OPT(nir_lower_gs_intrinsics, 0);

   /* See also brw_nir_workarounds.py */
   if (compiler->precise_trig &&
       !(devinfo->ver >= 10 || devinfo->platform == INTEL_PLATFORM_KBL))
      OPT(brw_nir_apply_trig_workarounds);

   /* This workaround existing for performance reasons. Since it requires not
    * setting RENDER_SURFACE_STATE::SurfaceArray when the array length is 1,
    * we're loosing the HW robustness feature in that case.
    *
    * So when robust image access is enabled, just avoid the workaround.
    */
   if (intel_needs_workaround(devinfo, 1806565034) && !opts->robust_image_access)
      OPT(intel_nir_clamp_image_1d_2d_array_sizes);

   OPT(nir_normalize_cubemap_coords);

   OPT(nir_lower_global_vars_to_local);

   OPT(nir_split_var_copies);
   OPT(nir_split_struct_vars, nir_var_function_temp);

   brw_nir_optimize(nir, devinfo);

   unsigned lower_flrp =
      (nir->options->lower_flrp16 ? 16 : 0) |
      (nir->options->lower_flrp32 ? 32 : 0) |
      (nir->options->lower_flrp64 ? 64 : 0);

   OPT(nir_lower_flrp, lower_flrp, false /* always_precise */);

   struct nir_opt_16bit_tex_image_options options = {
      .rounding_mode = nir_rounding_mode_undef,
      .opt_tex_dest_types = nir_type_float | nir_type_int | nir_type_uint,
   };
   OPT(nir_opt_16bit_tex_image, &options);

   OPT(nir_lower_doubles, opts->softfp64, nir->options->lower_doubles_options);
   if (OPT(nir_lower_int64_float_conversions)) {
      OPT(nir_opt_algebraic);
      OPT(nir_lower_doubles, opts->softfp64,
          nir->options->lower_doubles_options);
   }

   OPT(nir_lower_bit_size, lower_bit_size_callback, (void *)compiler);

   /* Lower a bunch of stuff */
   OPT(nir_lower_var_copies);

   /* This needs to be run after the first optimization pass but before we
    * lower indirect derefs away
    */
   OPT(nir_opt_large_constants, NULL, 32);

   OPT(nir_lower_load_const_to_scalar);

   OPT(nir_lower_system_values);
   nir_lower_compute_system_values_options lower_csv_options = {
      .has_base_workgroup_id = nir->info.stage == MESA_SHADER_COMPUTE,
   };
   OPT(nir_lower_compute_system_values, &lower_csv_options);

   const nir_lower_subgroups_options subgroups_options = {
      .subgroup_size = brw_nir_api_subgroup_size(nir, 0),
      .ballot_bit_size = 32,
      .ballot_components = 1,
      .lower_to_scalar = true,
      .lower_relative_shuffle = true,
      .lower_quad_broadcast_dynamic = true,
      .lower_elect = true,
      .lower_inverse_ballot = true,
      .lower_rotate_to_shuffle = true,
   };
   OPT(nir_lower_subgroups, &subgroups_options);

   nir_variable_mode indirect_mask =
      brw_nir_no_indirect_mask(compiler, nir->info.stage);
   OPT(nir_lower_indirect_derefs_to_if_else_trees, indirect_mask, UINT32_MAX);

   /* Even in cases where we can handle indirect temporaries via scratch, we
    * it can still be expensive.  Lower indirects on small arrays to
    * conditional load/stores.
    *
    * The threshold of 16 was chosen semi-arbitrarily.  The idea is that an
    * indirect on an array of 16 elements is about 30 instructions at which
    * point, you may be better off doing a send.  With a SIMD8 program, 16
    * floats is 1/8 of the entire register file.  Any array larger than that
    * is likely to cause pressure issues.  Also, this value is sufficiently
    * high that the benchmarks known to suffer from large temporary array
    * issues are helped but nothing else in shader-db is hurt except for maybe
    * that one kerbal space program shader.
    */
   if (!(indirect_mask & nir_var_function_temp))
      OPT(nir_lower_indirect_derefs_to_if_else_trees, nir_var_function_temp, 16);

   /* Lower array derefs of vectors for SSBO and UBO loads.  For both UBOs and
    * SSBOs, our back-end is capable of loading an entire vec4 at a time and
    * we would like to take advantage of that whenever possible regardless of
    * whether or not the app gives us full loads.  This should allow the
    * optimizer to combine UBO and SSBO load operations and save us some send
    * messages.
    */
   OPT(nir_lower_array_deref_of_vec,
       nir_var_mem_ubo | nir_var_mem_ssbo, NULL,
       nir_lower_direct_array_deref_of_vec_load);

   /* Clamp load_per_vertex_input of the TCS stage so that we do not generate
    * loads reading out of bounds. We can do this here because we called
    * nir_lower_system_values above.
    */
   if (nir->info.stage == MESA_SHADER_TESS_CTRL &&
       compiler->use_tcs_multi_patch)
      OPT(intel_nir_clamp_per_vertex_loads);

   /* Get rid of split copies */
   brw_nir_optimize(nir, devinfo);
}

static bool
brw_nir_zero_inputs_instr(struct nir_builder *b, nir_intrinsic_instr *intrin,
                          void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   if (!nir_deref_mode_is(deref, nir_var_shader_in))
      return false;

   if (deref->deref_type != nir_deref_type_var)
      return false;

   nir_variable *var = deref->var;

   uint64_t zero_inputs = *(uint64_t *)data;
   if (!(BITFIELD64_BIT(var->data.location) & zero_inputs))
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *zero = nir_imm_zero(b, 1, 32);

   nir_def_replace(&intrin->def, zero);

   return true;
}

static bool
brw_nir_zero_inputs(nir_shader *shader, uint64_t *zero_inputs)
{
   return nir_shader_intrinsics_pass(shader, brw_nir_zero_inputs_instr,
                                     nir_metadata_control_flow,
                                     zero_inputs);
}

/* Code for Wa_18019110168 may have created input/output variables beyond
 * VARYING_SLOT_MAX and removed uses of variables below VARYING_SLOT_MAX.
 * Clean it up, so they all stay below VARYING_SLOT_MAX.
 */
static void
brw_mesh_compact_io(nir_shader *mesh, nir_shader *frag)
{
   gl_varying_slot mapping[VARYING_SLOT_MAX] = {0, };
   gl_varying_slot cur = VARYING_SLOT_VAR0;
   bool compact = false;

   nir_foreach_shader_out_variable(var, mesh) {
      gl_varying_slot location = var->data.location;
      if (location < VARYING_SLOT_VAR0)
         continue;
      assert(location < ARRAY_SIZE(mapping));

      const struct glsl_type *type = var->type;
      if (nir_is_arrayed_io(var, MESA_SHADER_MESH)) {
         assert(glsl_type_is_array(type));
         type = glsl_get_array_element(type);
      }

      if (mapping[location])
         continue;

      unsigned num_slots = glsl_count_attribute_slots(type, false);

      compact |= location + num_slots > VARYING_SLOT_MAX;

      mapping[location] = cur;
      cur += num_slots;
   }

   if (!compact)
      return;

   /* The rest of this function should be hit only for Wa_18019110168. */

   nir_foreach_shader_out_variable(var, mesh) {
      gl_varying_slot location = var->data.location;
      if (location < VARYING_SLOT_VAR0)
         continue;
      location = mapping[location];
      if (location == 0)
         continue;
      var->data.location = location;
   }

   nir_foreach_shader_in_variable(var, frag) {
      gl_varying_slot location = var->data.location;
      if (location < VARYING_SLOT_VAR0)
         continue;
      location = mapping[location];
      if (location == 0)
         continue;
      var->data.location = location;
   }

   nir_shader_gather_info(mesh, nir_shader_get_entrypoint(mesh));
   nir_shader_gather_info(frag, nir_shader_get_entrypoint(frag));

   if (should_print_nir(mesh)) {
      printf("%s\n", __func__);
      nir_print_shader(mesh, stdout);
   }
   if (should_print_nir(frag)) {
      printf("%s\n", __func__);
      nir_print_shader(frag, stdout);
   }
}

void
brw_nir_link_shaders(const struct brw_compiler *compiler,
                     nir_shader *producer, nir_shader *consumer)
{
   const struct intel_device_info *devinfo = compiler->devinfo;

   if (producer->info.stage == MESA_SHADER_MESH &&
       consumer->info.stage == MESA_SHADER_FRAGMENT) {
      uint64_t fs_inputs = 0, ms_outputs = 0;
      /* gl_MeshPerPrimitiveEXT[].gl_ViewportIndex, gl_PrimitiveID and gl_Layer
       * are per primitive, but fragment shader does not have them marked as
       * such. Add the annotation here.
       */
      nir_foreach_shader_in_variable(var, consumer) {
         fs_inputs |= BITFIELD64_BIT(var->data.location);

         switch (var->data.location) {
            case VARYING_SLOT_LAYER:
            case VARYING_SLOT_PRIMITIVE_ID:
            case VARYING_SLOT_VIEWPORT:
               var->data.per_primitive = 1;
               break;
            default:
               continue;
         }
      }

      nir_foreach_shader_out_variable(var, producer)
         ms_outputs |= BITFIELD64_BIT(var->data.location);

      uint64_t zero_inputs = ~ms_outputs & fs_inputs;
      zero_inputs &= VARYING_BIT_LAYER |
                     VARYING_BIT_VIEWPORT;

      if (zero_inputs)
         NIR_PASS(_, consumer, brw_nir_zero_inputs, &zero_inputs);
   }

   nir_lower_io_array_vars_to_elements(producer, consumer);
   nir_validate_shader(producer, "after nir_lower_io_arrays_to_elements");
   nir_validate_shader(consumer, "after nir_lower_io_arrays_to_elements");

   NIR_PASS(_, producer, nir_lower_io_vars_to_scalar, nir_var_shader_out);
   NIR_PASS(_, consumer, nir_lower_io_vars_to_scalar, nir_var_shader_in);
   brw_nir_optimize(producer, devinfo);
   brw_nir_optimize(consumer, devinfo);

   if (nir_link_opt_varyings(producer, consumer))
      brw_nir_optimize(consumer, devinfo);

   NIR_PASS(_, producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
   NIR_PASS(_, consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

   if (nir_remove_unused_varyings(producer, consumer)) {
      if (should_print_nir(producer)) {
         printf("nir_remove_unused_varyings\n");
         nir_print_shader(producer, stdout);
      }
      if (should_print_nir(consumer)) {
         printf("nir_remove_unused_varyings\n");
         nir_print_shader(consumer, stdout);
      }

      NIR_PASS(_, producer, nir_lower_global_vars_to_local);
      NIR_PASS(_, consumer, nir_lower_global_vars_to_local);

      brw_nir_optimize(producer, devinfo);
      brw_nir_optimize(consumer, devinfo);

      if (producer->info.stage == MESA_SHADER_MESH &&
            consumer->info.stage == MESA_SHADER_FRAGMENT) {
         brw_mesh_compact_io(producer, consumer);
      }
   }

   NIR_PASS(_, producer, nir_opt_vectorize_io_vars, nir_var_shader_out);

   if (producer->info.stage == MESA_SHADER_TESS_CTRL &&
       producer->options->vectorize_tess_levels)
   NIR_PASS(_, producer, nir_lower_tess_level_array_vars_to_vec);

   NIR_PASS(_, producer, nir_opt_combine_stores, nir_var_shader_out);
   NIR_PASS(_, consumer, nir_opt_vectorize_io_vars, nir_var_shader_in);

   if (producer->info.stage != MESA_SHADER_TESS_CTRL &&
       producer->info.stage != MESA_SHADER_MESH &&
       producer->info.stage != MESA_SHADER_TASK) {
      /* Calling lower_io_to_vector creates output variable writes with
       * write-masks.  On non-TCS outputs, the back-end can't handle it and we
       * need to call nir_lower_io_vars_to_temporaries to get rid of them.  This,
       * in turn, creates temporary variables and extra copy_deref intrinsics
       * that we need to clean up.
       *
       * Note Mesh/Task don't support I/O as temporaries (I/O is shared
       * between whole workgroup, possibly using multiple HW threads). For
       * those write-mask in output is handled by I/O lowering.
       */
      NIR_PASS(_, producer, nir_lower_io_vars_to_temporaries,
                 nir_shader_get_entrypoint(producer), nir_var_shader_out);
      NIR_PASS(_, producer, nir_lower_global_vars_to_local);
      NIR_PASS(_, producer, nir_split_var_copies);
      NIR_PASS(_, producer, nir_lower_var_copies);
   }

   if (producer->info.stage == MESA_SHADER_TASK &&
         consumer->info.stage == MESA_SHADER_MESH) {

      for (unsigned i = 0; i < 3; ++i)
         assert(producer->info.mesh.ts_mesh_dispatch_dimensions[i] <= UINT16_MAX);

      nir_lower_compute_system_values_options options = {
            .lower_workgroup_id_to_index = true,
            .num_workgroups[0] = producer->info.mesh.ts_mesh_dispatch_dimensions[0],
            .num_workgroups[1] = producer->info.mesh.ts_mesh_dispatch_dimensions[1],
            .num_workgroups[2] = producer->info.mesh.ts_mesh_dispatch_dimensions[2],
            /* nir_lower_idiv generates expensive code */
            .shortcut_1d_workgroup_id = compiler->devinfo->verx10 >= 125,
      };

      NIR_PASS(_, consumer, nir_lower_compute_system_values, &options);
   }
}

bool
brw_nir_should_vectorize_mem(unsigned align_mul, unsigned align_offset,
                             unsigned bit_size,
                             unsigned num_components,
                             int64_t hole_size,
                             nir_intrinsic_instr *low,
                             nir_intrinsic_instr *high,
                             void *data)
{
   /* Don't combine things to generate 64-bit loads/stores.  We have to split
    * those back into 32-bit ones anyway and UBO loads aren't split in NIR so
    * we don't want to make a mess for the back-end.
    */
   if (bit_size > 32)
      return false;

   if (low->intrinsic == nir_intrinsic_load_ubo_uniform_block_intel ||
       low->intrinsic == nir_intrinsic_load_ssbo_uniform_block_intel ||
       low->intrinsic == nir_intrinsic_load_shared_uniform_block_intel ||
       low->intrinsic == nir_intrinsic_load_global_constant_uniform_block_intel) {
      if (num_components > 4) {
         if (bit_size != 32)
            return false;

         if (num_components > 32)
            return false;

         if (hole_size >= 8 * 4)
            return false;
      }
   } else {
      /* We can handle at most a vec4 right now.  Anything bigger would get
       * immediately split by brw_nir_lower_mem_access_bit_sizes anyway.
       */
      if (num_components > 4)
         return false;

      if (hole_size > 4)
         return false;
   }


   const uint32_t align = nir_combined_align(align_mul, align_offset);

   if (align < bit_size / 8)
      return false;

   return true;
}

static
bool combine_all_memory_barriers(nir_intrinsic_instr *a,
                                 nir_intrinsic_instr *b,
                                 void *data)
{
   /* Combine control barriers with identical memory semantics. This prevents
    * the second barrier generating a spurious, identical fence message as the
    * first barrier.
    */
   if (nir_intrinsic_memory_modes(a) == nir_intrinsic_memory_modes(b) &&
       nir_intrinsic_memory_semantics(a) == nir_intrinsic_memory_semantics(b) &&
       nir_intrinsic_memory_scope(a) == nir_intrinsic_memory_scope(b)) {
      nir_intrinsic_set_execution_scope(a, MAX2(nir_intrinsic_execution_scope(a),
                                                nir_intrinsic_execution_scope(b)));
      return true;
   }

   /* Only combine pure memory barriers */
   if ((nir_intrinsic_execution_scope(a) != SCOPE_NONE) ||
       (nir_intrinsic_execution_scope(b) != SCOPE_NONE))
      return false;

   /* Translation to backend IR will get rid of modes we don't care about, so
    * no harm in always combining them.
    *
    * TODO: While HW has only ACQUIRE|RELEASE fences, we could improve the
    * scheduling so that it can take advantage of the different semantics.
    */
   nir_intrinsic_set_memory_modes(a, nir_intrinsic_memory_modes(a) |
                                     nir_intrinsic_memory_modes(b));
   nir_intrinsic_set_memory_semantics(a, nir_intrinsic_memory_semantics(a) |
                                         nir_intrinsic_memory_semantics(b));
   nir_intrinsic_set_memory_scope(a, MAX2(nir_intrinsic_memory_scope(a),
                                          nir_intrinsic_memory_scope(b)));
   return true;
}

static nir_mem_access_size_align
get_mem_access_size_align(nir_intrinsic_op intrin, uint8_t bytes,
                          uint8_t bit_size, uint32_t align_mul, uint32_t align_offset,
                          bool offset_is_const, enum gl_access_qualifier access,
                          const void *cb_data)
{
   const uint32_t align = nir_combined_align(align_mul, align_offset);
   const struct brw_mem_access_cb_data *mem_cb_data =
      (struct brw_mem_access_cb_data *)cb_data;
   const struct intel_device_info *devinfo = mem_cb_data->devinfo;

   switch (intrin) {
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_scratch:
      /* The offset is constant so we can use a 32-bit load and just shift it
       * around as needed.
       */
      if (align < 4 && offset_is_const) {
         assert(util_is_power_of_two_nonzero(align_mul) && align_mul >= 4);
         const unsigned pad = align_offset % 4;
         const unsigned comps32 = MIN2(DIV_ROUND_UP(bytes + pad, 4), 4);
         return (nir_mem_access_size_align) {
            .bit_size = 32,
            .num_components = comps32,
            .align = 4,
            .shift = nir_mem_access_shift_method_scalar,
         };
      }
      break;

   case nir_intrinsic_load_task_payload:
      if (bytes < 4 || align < 4) {
         return (nir_mem_access_size_align) {
            .bit_size = 32,
            .num_components = 1,
            .align = 4,
            .shift = nir_mem_access_shift_method_scalar,
         };
      }
      break;

   default:
      break;
   }

   const bool is_load = nir_intrinsic_infos[intrin].has_dest;
   const bool is_scratch = intrin == nir_intrinsic_load_scratch ||
                           intrin == nir_intrinsic_store_scratch;

   if (align < 4 || bytes < 4) {
      /* Choose a byte, word, or dword */
      bytes = MIN2(bytes, 4);
      if (bytes == 3)
         bytes = is_load ? 4 : 2;

      if (is_scratch) {
         /* The way scratch address swizzling works in the back-end, it
          * happens at a DWORD granularity so we can't have a single load
          * or store cross a DWORD boundary.
          */
         if ((align_offset % 4) + bytes > MIN2(align_mul, 4))
            bytes = MIN2(align_mul, 4) - (align_offset % 4);

         /* Must be a power of two */
         if (bytes == 3)
            bytes = 2;
      }

      return (nir_mem_access_size_align) {
         .bit_size = bytes * 8,
         .num_components = 1,
         .align = 1,
         .shift = nir_mem_access_shift_method_scalar,
      };
   } else {
      bytes = MIN2(bytes, 16);

      /* With UGM LSC dataport, we don't need to lower 64bit data access into
       * two 32bit single vector access since it supports direct 64bit data
       * operation.
       */
      if (devinfo->has_lsc && align == 8 && bit_size == 64) {
         return (nir_mem_access_size_align) {
            .bit_size = bit_size,
            .num_components = bytes / 8,
            .align = bit_size / 8,
            .shift = nir_mem_access_shift_method_scalar,
         };
      } else {
         return (nir_mem_access_size_align) {
            .bit_size = 32,
            .num_components = is_scratch ? 1 :
                              is_load ? DIV_ROUND_UP(bytes, 4) : bytes / 4,
            .align = 4,
            .shift = nir_mem_access_shift_method_scalar,
         };
      }
   }
}

static bool
brw_nir_ssbo_intel_instr(nir_builder *b,
                         nir_intrinsic_instr *intrin,
                         void *cb_data)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ssbo: {
      b->cursor = nir_before_instr(&intrin->instr);
      nir_def *value = nir_load_ssbo_intel(
         b,
         intrin->def.num_components,
         intrin->def.bit_size,
         intrin->src[0].ssa,
         intrin->src[1].ssa,
         .access = nir_intrinsic_access(intrin),
         .align_mul = nir_intrinsic_align_mul(intrin),
         .align_offset = nir_intrinsic_align_offset(intrin),
         .base = 0);
      value->loop_invariant = intrin->def.loop_invariant;
      value->divergent = intrin->def.divergent;
      nir_def_replace(&intrin->def, value);
      return true;
   }

   case nir_intrinsic_store_ssbo: {
      b->cursor = nir_instr_remove(&intrin->instr);
      nir_store_ssbo_intel(
         b,
         intrin->src[0].ssa,
         intrin->src[1].ssa,
         intrin->src[2].ssa,
         .access = nir_intrinsic_access(intrin),
         .align_mul = nir_intrinsic_align_mul(intrin),
         .align_offset = nir_intrinsic_align_offset(intrin),
         .base = 0);
      return true;
   }

   default:
      return false;
   }
}

static bool
brw_nir_ssbo_intel(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader,
                                     brw_nir_ssbo_intel_instr,
                                     nir_metadata_control_flow,
                                     NULL);
}

static void
brw_vectorize_lower_mem_access(nir_shader *nir,
                               const struct brw_compiler *compiler,
                               enum brw_robustness_flags robust_flags)
{
   bool progress = false;

   nir_load_store_vectorize_options options = {
      .modes = nir_var_mem_ubo | nir_var_mem_ssbo |
               nir_var_mem_global | nir_var_mem_shared |
               nir_var_mem_task_payload,
      .callback = brw_nir_should_vectorize_mem,
      .robust_modes = (nir_variable_mode)0,
   };

   if (robust_flags & BRW_ROBUSTNESS_UBO)
      options.robust_modes |= nir_var_mem_ubo;
   if (robust_flags & BRW_ROBUSTNESS_SSBO)
      options.robust_modes |= nir_var_mem_ssbo;

   OPT(nir_opt_load_store_vectorize, &options);

   /* When HW supports block loads, using the divergence analysis, try
    * to find uniform SSBO loads and turn them into block loads.
    *
    * Rerun the vectorizer after that to make the largest possible block
    * loads.
    *
    * This is a win on 2 fronts :
    *   - fewer send messages
    *   - reduced register pressure
    */
   if (OPT(intel_nir_blockify_uniform_loads, compiler->devinfo)) {
      OPT(nir_opt_load_store_vectorize, &options);

      OPT(nir_opt_constant_folding);
      OPT(nir_opt_copy_prop);

      if (OPT(brw_nir_rebase_const_offset_ubo_loads)) {
         OPT(nir_opt_cse);
         OPT(nir_opt_copy_prop);

         nir_load_store_vectorize_options ubo_options = {
            .modes = nir_var_mem_ubo,
            .callback = brw_nir_should_vectorize_mem,
            .robust_modes = options.robust_modes & nir_var_mem_ubo,
         };

         OPT(nir_opt_load_store_vectorize, &ubo_options);
      }
   }

   struct brw_mem_access_cb_data cb_data = {
      .devinfo = compiler->devinfo,
   };

   nir_lower_mem_access_bit_sizes_options mem_access_options = {
      .modes = nir_var_mem_ssbo |
               nir_var_mem_constant |
               nir_var_mem_task_payload |
               nir_var_shader_temp |
               nir_var_function_temp |
               nir_var_mem_global |
               nir_var_mem_shared,
      .callback = get_mem_access_size_align,
      .cb_data = &cb_data,
   };
   OPT(nir_lower_mem_access_bit_sizes, &mem_access_options);

   while (progress) {
      progress = false;

      OPT(nir_lower_pack);
      OPT(nir_opt_copy_prop);
      OPT(nir_opt_dce);
      OPT(nir_opt_cse);
      OPT(nir_opt_algebraic);
      OPT(nir_opt_constant_folding);
   }

   /* Do this after the vectorization & brw_nir_rebase_const_offset_ubo_loads
    * so that we maximize the offset put into the messages.
    */
   if (compiler->devinfo->ver >= 20) {
      OPT(brw_nir_ssbo_intel);

      const nir_opt_offsets_options offset_options = {
         .buffer_max        = UINT32_MAX,
         .shared_max        = UINT32_MAX,
         .shared_atomic_max = UINT32_MAX,
      };
      OPT(nir_opt_offsets, &offset_options);

      OPT(brw_nir_lower_immediate_offsets);
   }
}

static bool
nir_shader_has_local_variables(const nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir) {
      if (!exec_list_is_empty(&impl->locals))
         return true;
   }

   return false;
}

static bool
lower_txd_cb(const nir_tex_instr *tex, const void *data)
{
   const struct intel_device_info *devinfo = data;

   int min_lod_index = nir_tex_instr_src_index(tex, nir_tex_src_min_lod);
   if (tex->is_shadow && min_lod_index >= 0)
      return true;

   int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (tex->is_shadow && offset_index >= 0 && min_lod_index >= 0)
      return true;

   /* Cases that require a sampler header and the payload is already too large
    * for the HW to handle.
    */
   const int sampler_offset_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_offset);
   if (min_lod_index >= 0 && sampler_offset_idx >= 0) {
      if (!nir_src_is_const(tex->src[sampler_offset_idx].src) ||
          (nir_src_is_const(tex->src[sampler_offset_idx].src) &&
           (tex->sampler_index +
            nir_src_as_uint(tex->src[sampler_offset_idx].src)) >= 16))
         return true;
   }

   const int sampler_handle_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_handle);
   if (sampler_handle_idx >= 0 && min_lod_index >= 0)
      return true;

   if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE)
      return true;

   if (devinfo->verx10 >= 125) {
      /* For below, See bspec 45942, "Enable new message layout for cube
       * array"
       */
      if (tex->sampler_dim == GLSL_SAMPLER_DIM_3D)
         return true;

      if (tex->is_array)
         return true;
   }

   if (tex->is_shadow && offset_index >= 0 &&
       !brw_nir_tex_offset_in_constant_range(tex, offset_index))
      return true;

   return false;
}

static bool
flag_fused_eu_disable_instr(nir_builder *b, nir_instr *instr, void *data)
{
   switch (instr->type) {
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);

      for (unsigned i = 0; i < tex->num_srcs; ++i) {
         nir_tex_src_type src_type = tex->src[i].src_type;

         /* backend2 is the packed dynamically programmable offset, goes into
          * the sampler message header, so it needs to be considered for EU
          * fusion.
          */
         if (src_type != nir_tex_src_texture_handle &&
             src_type != nir_tex_src_sampler_handle &&
             src_type != nir_tex_src_texture_offset &&
             src_type != nir_tex_src_sampler_offset &&
             src_type != nir_tex_src_backend2)
            continue;

         if (nir_src_is_divergent(&tex->src[i].src)) {
            tex->backend_flags |= BRW_TEX_INSTR_FUSED_EU_DISABLE;
            return true;
         }
      }
      return false;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      /* We only need to care of intrinsics that refers to a structure/descriptor
       * outside of the EU's registers like RENDER_SURFACE_STATE/SAMPLER_STATE,
       * because the fusing will pick one thread's descriptor handle and use that
       * for the 2 fused threads.
       *
       * Global pointers don't have that problem since all the access' data is
       * per lane in the payload of the SEND message (the 64bit pointer).
       *
       * URB/shared-memory don't have that problem either because there is no
       * descriptor information outside the EU, it's just a per lane
       * handle/offset.
       */
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ssbo_uniform_block_intel:
      case nir_intrinsic_load_ubo_uniform_block_intel:
      case nir_intrinsic_load_ssbo_block_intel:
      case nir_intrinsic_load_ssbo_intel:
      case nir_intrinsic_store_ssbo_intel:
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_store_ssbo:
      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_image_load:
      case nir_intrinsic_image_store:
      case nir_intrinsic_image_size:
      case nir_intrinsic_image_levels:
      case nir_intrinsic_image_atomic:
      case nir_intrinsic_image_atomic_swap:
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_bindless_image_store:
      case nir_intrinsic_bindless_image_size:
      case nir_intrinsic_bindless_image_levels:
      case nir_intrinsic_bindless_image_atomic:
      case nir_intrinsic_bindless_image_atomic_swap: {
         int src_idx = nir_get_io_index_src_number(intrin);
         assert(src_idx >= 0);
         if (nir_src_is_divergent(&intrin->src[src_idx])) {
            nir_intrinsic_set_access(intrin,
                                     nir_intrinsic_access(intrin) |
                                     ACCESS_FUSED_EU_DISABLE_INTEL);
            return true;
         }
         return false;
      }

      default:
         return false;
      }
   }

   default:
      return false;
   }
}

static void
brw_nir_lower_int64(nir_shader *nir, const struct intel_device_info *devinfo)
{
   UNUSED bool progress; /* Written by OPT */

   /* Potentially perform this optimization pass twice because it can create
    * additional opportunities for itself.
    */
   if (OPT(nir_opt_algebraic_before_lower_int64))
      OPT(nir_opt_algebraic_before_lower_int64);

   if (OPT(nir_lower_int64))
      brw_nir_optimize(nir, devinfo);
}

/* Prepare the given shader for codegen
 *
 * This function is intended to be called right before going into the actual
 * backend and is highly backend-specific.
 */
void
brw_postprocess_nir_opts(nir_shader *nir, const struct brw_compiler *compiler,
                         enum brw_robustness_flags robust_flags)
{
   const struct intel_device_info *devinfo = compiler->devinfo;

   UNUSED bool progress; /* Written by OPT */

   const nir_lower_tex_options tex_options = {
      .lower_txp = ~0,
      .lower_txf_offset = true,
      .lower_rect_offset = true,
      .lower_txb_shadow_clamp = true,
      .lower_tg4_offsets = true,
      .lower_txs_lod = true, /* Wa_14012320009 */
      .lower_offset_filter =
         devinfo->verx10 >= 125 ? lower_xehp_tg4_offset_filter : NULL,
      .lower_invalid_implicit_lod = true,
      .lower_index_to_offset = true,
      .lower_txd_cb = lower_txd_cb,
      .lower_txd_data = devinfo,
   };

   /* In the case where TG4 coords are lowered to offsets and we have a
    * lower_xehp_tg4_offset_filter lowering those offsets further, we need to
    * rerun the pass because the instructions inserted by the first lowering
    * are not visible during that first pass.
    */
   if (OPT(nir_lower_tex, &tex_options))
      OPT(nir_lower_tex, &tex_options);

   /* MCS lowering can introduce u2u16 conversions. We need to lower those to
    * make constant offsets detectable by brw_nir_texture_backend_opcode().
    */
   if (OPT(brw_nir_lower_mcs_fetch, devinfo))
      OPT(nir_opt_constant_folding);

   OPT(intel_nir_lower_sparse_intrinsics);

   /* Needs to happen before the backend opcode selection */
   OPT(brw_nir_pre_lower_texture);

   /* Needs to happen before the texture lowering */
   OPT(brw_nir_texture_backend_opcode, devinfo);

   OPT(brw_nir_lower_texture);

   OPT(nir_lower_bit_size, lower_bit_size_callback, (void *)compiler);

   OPT(nir_opt_combine_barriers, combine_all_memory_barriers, NULL);

   do {
      progress = false;
      OPT(nir_opt_algebraic_before_ffma);
   } while (progress);

   if (devinfo->verx10 >= 125) {
      /* Lower integer division by constants before nir_lower_idiv. */
      OPT(nir_opt_idiv_const, 32);
      const nir_lower_idiv_options options = {
         .allow_fp16 = false
      };

      /* Given an 8-bit integer remainder, nir_lower_idiv will produce new
       * 8-bit integer math which needs to be lowered.
       */
      if (OPT(nir_lower_idiv, &options))
         OPT(nir_lower_bit_size, lower_bit_size_callback, (void *)compiler);
   }

   if (devinfo->ver >= 30)
      NIR_PASS(_, nir, brw_nir_lower_sample_index_in_coord);

   if (mesa_shader_stage_can_set_fragment_shading_rate(nir->info.stage))
      NIR_PASS(_, nir, intel_nir_lower_shading_rate_output);

   OPT(brw_nir_tag_speculative_access);

   brw_nir_optimize(nir, devinfo);

   if (nir_shader_has_local_variables(nir)) {
      OPT(nir_lower_vars_to_explicit_types, nir_var_function_temp,
          glsl_get_natural_size_align_bytes);
      OPT(nir_lower_explicit_io, nir_var_function_temp,
          nir_address_format_32bit_offset);
      brw_nir_optimize(nir, devinfo);
   }

   brw_vectorize_lower_mem_access(nir, compiler, robust_flags);

   /* Needs to be prior int64 lower because it generates 64bit address
    * manipulations
    */
   OPT(intel_nir_lower_printf);

   brw_nir_lower_int64(nir, devinfo);

   /* This pass specifically looks for sequences of fmul and fadd that
    * intel_nir_opt_peephole_ffma will try to eliminate. Call this
    * reassociation pass first.
    */
   OPT(nir_opt_reassociate_matrix_mul);

   /* Try and fuse multiply-adds, if successful, run shrink_vectors to
    * avoid peephole_ffma to generate things like this :
    *    vec16 ssa_0 = ...
    *    vec16 ssa_1 = fneg ssa_0
    *    vec1  ssa_2 = ffma ssa_1, ...
    *
    * We want this instead :
    *    vec16 ssa_0 = ...
    *    vec1  ssa_1 = fneg ssa_0.x
    *    vec1  ssa_2 = ffma ssa_1, ...
    */
   if (OPT(intel_nir_opt_peephole_ffma))
      OPT(nir_opt_shrink_vectors, false);

   OPT(intel_nir_opt_peephole_imul32x16);

   if (OPT(nir_opt_comparison_pre)) {
      OPT(nir_opt_copy_prop);
      OPT(nir_opt_dce);
      OPT(nir_opt_cse);

      /* Do the select peepehole again.  nir_opt_comparison_pre (combined with
       * the other optimization passes) will have removed at least one
       * instruction from one of the branches of the if-statement, so now it
       * might be under the threshold of conversion to bcsel.
       */
      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 0,
      };
      OPT(nir_opt_peephole_select, &peephole_select_options);

      peephole_select_options.limit = 1;
      peephole_select_options.expensive_alu_ok = true;
      OPT(nir_opt_peephole_select, &peephole_select_options);
   }

   do {
      progress = false;

      OPT(brw_nir_opt_fsat);
      OPT(nir_opt_algebraic_late);
      OPT(brw_nir_lower_fsign);

      if (progress) {
         OPT(nir_opt_constant_folding);
         OPT(nir_opt_copy_prop);
         OPT(nir_opt_dce);
         OPT(nir_opt_cse);
      }
   } while (progress);


   OPT(nir_lower_fp16_casts, nir_lower_fp16_split_fp64);

   OPT(nir_lower_alu_to_scalar, NULL, NULL);

   while (OPT(nir_opt_algebraic_distribute_src_mods)) {
      OPT(nir_opt_constant_folding);
      OPT(nir_opt_copy_prop);
      OPT(nir_opt_dce);
      OPT(nir_opt_cse);
   }

   OPT(nir_opt_copy_prop);
   OPT(nir_opt_dce);

   nir_move_options move_all = nir_move_const_undef | nir_move_load_ubo |
                               nir_move_load_input | nir_move_comparisons |
                               nir_move_copies | nir_move_load_ssbo |
                               nir_move_alu;

   OPT(nir_opt_sink, move_all);
   OPT(nir_opt_move, move_all);
   OPT(nir_opt_dead_cf);

   static const nir_lower_subgroups_options subgroups_options = {
      .ballot_bit_size = 32,
      .ballot_components = 1,
      .lower_elect = true,
      .lower_subgroup_masks = true,
   };

   if (OPT(nir_opt_uniform_atomics, false))
      OPT(nir_lower_subgroups, &subgroups_options);

   /* nir_opt_uniform_subgroup can create some operations (e.g.,
    * load_subgroup_lt_mask) that need to be lowered again.
    */
   if (OPT(nir_opt_uniform_subgroup, &subgroups_options)) {
      /* nir_opt_uniform_subgroup may have made some things
       * that previously appeared divergent be marked as convergent. This
       * allows the elimination of some loops over, say, a TXF instruction
       * with a non-uniform texture handle.
       */
      brw_nir_optimize(nir, devinfo);

      OPT(nir_lower_subgroups, &subgroups_options);
   }

   /* A few passes that run after the initial int64 lowering may produce
    * new int64 operations.  E.g. uniform subgroup may generate a 64-bit mul
    * and peephole_select may generate a 64-bit select.  So do another
    * round at the tail end.
    */
   brw_nir_lower_int64(nir, devinfo);

   /* Deal with EU fusion */
   if (devinfo->ver == 12) {
      nir_divergence_options options =
         nir_divergence_across_subgroups |
         nir_divergence_multiple_workgroup_per_compute_subgroup;

      nir_foreach_function_impl(impl, nir) {
         nir_divergence_analysis_impl(impl, options);
         impl->valid_metadata |= nir_metadata_divergence;
      }

      nir_shader_instructions_pass(nir,
                                   flag_fused_eu_disable_instr,
                                   nir_metadata_all, NULL);

      /* We request a special divergence information which is not needed
       * after.
       */
      nir_foreach_function_impl(impl, nir) {
         nir_progress(true, impl, ~nir_metadata_divergence);
      }
   }
}

void
brw_postprocess_nir_out_of_ssa(nir_shader *nir,
                               unsigned dispatch_width,
                               debug_archiver *archiver,
                               bool debug_enabled)
{
   UNUSED bool progress; /* Written by OPT */

   /* Run fsign lowering again after the last time brw_nir_optimize is called.
    * As is the case with conversion lowering (below), brw_nir_optimize can
    * create additional fsign instructions.
    */
   if (OPT(brw_nir_lower_fsign))
      OPT(nir_opt_dce);

   /* Run nir_split_conversions only after the last tiem
    * brw_nir_optimize is called. Various optimizations invoked there can
    * rematerialize the conversions that the lowering pass eliminates.
    */
   const nir_split_conversions_options split_conv_opts = {
      .callback = intel_nir_split_conversions_cb,
   };
   OPT(nir_split_conversions, &split_conv_opts);

   /* Do this only after the last opt_gcm. GCM will undo this lowering. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      OPT(intel_nir_lower_non_uniform_barycentric_at_sample);
   }

   OPT(nir_lower_bool_to_int32);
   OPT(nir_opt_copy_prop);
   OPT(nir_opt_dce);

   OPT(nir_lower_locals_to_regs, 32);

   nir_validate_ssa_dominance(nir, "before nir_convert_from_ssa");

   /* Rerun the divergence analysis before convert_from_ssa as this pass has
    * some assert on consistent divergence flags.
    */
   NIR_PASS(_, nir, nir_convert_to_lcssa, true, true);
   nir_divergence_analysis(nir);

   if (unlikely(debug_enabled || archiver)) {
      /* Re-index SSA defs so we print more sensible numbers. */
      nir_foreach_function_impl(impl, nir) {
         nir_index_ssa_defs(impl);
      }

      if (debug_enabled) {
         fprintf(stderr, "NIR (SSA form) for %s shader:\n",
                 _mesa_shader_stage_to_string(nir->info.stage));
         nir_print_shader(nir, stderr);
      }

      if (unlikely(archiver))
         brw_debug_archive_nir(archiver, nir, dispatch_width, "ssa");
   }

   OPT(nir_convert_from_ssa, true, true);

   OPT(nir_opt_dce);

   if (OPT(nir_opt_rematerialize_compares))
      OPT(nir_opt_dce);

   /* The mesh stages require this pass to be called at the last minute,
    * but if anything is done by it, it will also constant fold, and that
    * undoes the work done by nir_trivialize_registers, so call it right
    * before that one instead.
    */
   if (nir->info.stage == MESA_SHADER_MESH ||
       nir->info.stage == MESA_SHADER_TASK)
      brw_nir_adjust_payload(nir);

   nir_trivialize_registers(nir);

   nir_sweep(nir);

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "NIR (final form) for %s shader:\n",
              _mesa_shader_stage_to_string(nir->info.stage));
      nir_print_shader(nir, stderr);
   }

   if (unlikely(archiver))
      brw_debug_archive_nir(archiver, nir, dispatch_width, "out");
}

static unsigned
get_subgroup_size(const struct shader_info *info, unsigned max_subgroup_size)
{
   if (info->api_subgroup_size) {
      /* We have to use the global/required constant size. */
      assert(info->api_subgroup_size >= 8 && info->api_subgroup_size <= 32);
      return info->api_subgroup_size;
   } else if (info->api_subgroup_size_draw_uniform) {
      /* It has to be uniform across all invocations but can vary per stage
       * if we want.  This gives us a bit more freedom.
       *
       * For compute, brw_nir_apply_key is called per-dispatch-width so this
       * is the actual subgroup size and not a maximum.  However, we only
       * invoke one size of any given compute shader so it's still guaranteed
       * to be uniform across invocations.
       */
      return max_subgroup_size;
   } else {
      /* The subgroup size is allowed to be fully varying.  For geometry
       * stages, we know it's always 8 which is max_subgroup_size so we can
       * return that.  For compute, brw_nir_apply_key is called once per
       * dispatch-width so max_subgroup_size is the real subgroup size.
       *
       * For fragment, we return 0 and let it fall through to the back-end
       * compiler.  This means we can't optimize based on subgroup size but
       * that's a risk the client took when it asked for a varying subgroup
       * size.
       */
      return info->stage == MESA_SHADER_FRAGMENT ? 0 : max_subgroup_size;
   }
}

unsigned
brw_nir_api_subgroup_size(const nir_shader *nir,
                          unsigned hw_subgroup_size)
{
   return get_subgroup_size(&nir->info, hw_subgroup_size);
}

void
brw_nir_apply_key(nir_shader *nir,
                  const struct brw_compiler *compiler,
                  const struct brw_base_prog_key *key,
                  unsigned max_subgroup_size)
{
   bool progress = false;

   const nir_lower_subgroups_options subgroups_options = {
      .subgroup_size = get_subgroup_size(&nir->info, max_subgroup_size),
      .ballot_bit_size = 32,
      .ballot_components = 1,
      .lower_subgroup_masks = true,
   };
   OPT(nir_lower_subgroups, &subgroups_options);

   if (key->limit_trig_input_range)
      OPT(brw_nir_limit_trig_input_range_workaround);

   if (progress) {
      brw_nir_optimize(nir, compiler->devinfo);
   }
}

enum brw_conditional_mod
brw_cmod_for_nir_comparison(nir_op op)
{
   switch (op) {
   case nir_op_flt:
   case nir_op_flt32:
   case nir_op_ilt:
   case nir_op_ilt32:
   case nir_op_ult:
   case nir_op_ult32:
      return BRW_CONDITIONAL_L;

   case nir_op_fge:
   case nir_op_fge32:
   case nir_op_ige:
   case nir_op_ige32:
   case nir_op_uge:
   case nir_op_uge32:
      return BRW_CONDITIONAL_GE;

   case nir_op_feq:
   case nir_op_feq32:
   case nir_op_ieq:
   case nir_op_ieq32:
   case nir_op_b32all_fequal2:
   case nir_op_b32all_iequal2:
   case nir_op_b32all_fequal3:
   case nir_op_b32all_iequal3:
   case nir_op_b32all_fequal4:
   case nir_op_b32all_iequal4:
      return BRW_CONDITIONAL_Z;

   case nir_op_fneu:
   case nir_op_fneu32:
   case nir_op_ine:
   case nir_op_ine32:
   case nir_op_b32any_fnequal2:
   case nir_op_b32any_inequal2:
   case nir_op_b32any_fnequal3:
   case nir_op_b32any_inequal3:
   case nir_op_b32any_fnequal4:
   case nir_op_b32any_inequal4:
      return BRW_CONDITIONAL_NZ;

   default:
      UNREACHABLE("Unsupported NIR comparison op");
   }
}

enum lsc_opcode
lsc_op_for_nir_intrinsic(const nir_intrinsic_instr *intrin)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_ssbo_intel:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_block_intel:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_global_constant_uniform_block_intel:
   case nir_intrinsic_load_shared_block_intel:
   case nir_intrinsic_load_shared_uniform_block_intel:
   case nir_intrinsic_load_ssbo_block_intel:
   case nir_intrinsic_load_ssbo_uniform_block_intel:
   case nir_intrinsic_load_ubo_uniform_block_intel:
   case nir_intrinsic_load_scratch:
      return LSC_OP_LOAD;

   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_ssbo_intel:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_block_intel:
   case nir_intrinsic_store_shared_block_intel:
   case nir_intrinsic_store_ssbo_block_intel:
   case nir_intrinsic_store_scratch:
      return LSC_OP_STORE;

   case nir_intrinsic_image_load:
   case nir_intrinsic_bindless_image_load:
      return nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS ?
             LSC_OP_LOAD_CMASK_MSRT :
             LSC_OP_LOAD_CMASK;

   case nir_intrinsic_image_store:
   case nir_intrinsic_bindless_image_store:
      return nir_intrinsic_image_dim(intrin) == GLSL_SAMPLER_DIM_MS ?
             LSC_OP_STORE_CMASK_MSRT :
             LSC_OP_STORE_CMASK;

   default:
      assert(nir_intrinsic_has_atomic_op(intrin));
      break;
   }

   switch (nir_intrinsic_atomic_op(intrin)) {
   case nir_atomic_op_iadd: {
      unsigned src_idx;
      switch (intrin->intrinsic) {
      case nir_intrinsic_image_atomic:
      case nir_intrinsic_bindless_image_atomic:
         src_idx = 3;
         break;
      case nir_intrinsic_ssbo_atomic:
         src_idx = 2;
         break;
      case nir_intrinsic_shared_atomic:
      case nir_intrinsic_global_atomic:
         src_idx = 1;
         break;
      default:
         UNREACHABLE("Invalid add atomic opcode");
      }

      if (nir_src_is_const(intrin->src[src_idx])) {
         int64_t add_val = nir_src_as_int(intrin->src[src_idx]);
         if (add_val == 1)
            return LSC_OP_ATOMIC_INC;
         else if (add_val == -1)
            return LSC_OP_ATOMIC_DEC;
      }
      return LSC_OP_ATOMIC_ADD;
   }

   case nir_atomic_op_imin: return LSC_OP_ATOMIC_MIN;
   case nir_atomic_op_umin: return LSC_OP_ATOMIC_UMIN;
   case nir_atomic_op_imax: return LSC_OP_ATOMIC_MAX;
   case nir_atomic_op_umax: return LSC_OP_ATOMIC_UMAX;
   case nir_atomic_op_iand: return LSC_OP_ATOMIC_AND;
   case nir_atomic_op_ior:  return LSC_OP_ATOMIC_OR;
   case nir_atomic_op_ixor: return LSC_OP_ATOMIC_XOR;
   case nir_atomic_op_xchg: return LSC_OP_ATOMIC_STORE;
   case nir_atomic_op_cmpxchg: return LSC_OP_ATOMIC_CMPXCHG;

   case nir_atomic_op_fmin: return LSC_OP_ATOMIC_FMIN;
   case nir_atomic_op_fmax: return LSC_OP_ATOMIC_FMAX;
   case nir_atomic_op_fcmpxchg: return LSC_OP_ATOMIC_FCMPXCHG;
   case nir_atomic_op_fadd: return LSC_OP_ATOMIC_FADD;

   default:
      UNREACHABLE("Unsupported NIR atomic intrinsic");
   }
}

enum brw_reg_type
brw_type_for_base_type(enum glsl_base_type base_type)
{
   switch (base_type) {
   case GLSL_TYPE_UINT:      return BRW_TYPE_UD;
   case GLSL_TYPE_INT:       return BRW_TYPE_D;
   case GLSL_TYPE_FLOAT:     return BRW_TYPE_F;
   case GLSL_TYPE_FLOAT16:   return BRW_TYPE_HF;
   case GLSL_TYPE_BFLOAT16:  return BRW_TYPE_BF;
   case GLSL_TYPE_DOUBLE:    return BRW_TYPE_DF;
   case GLSL_TYPE_UINT16:    return BRW_TYPE_UW;
   case GLSL_TYPE_INT16:     return BRW_TYPE_W;
   case GLSL_TYPE_UINT8:     return BRW_TYPE_UB;
   case GLSL_TYPE_INT8:      return BRW_TYPE_B;
   case GLSL_TYPE_UINT64:    return BRW_TYPE_UQ;
   case GLSL_TYPE_INT64:     return BRW_TYPE_Q;

   default:
      UNREACHABLE("invalid base type");
   }
}

enum brw_reg_type
brw_type_for_nir_type(const struct intel_device_info *devinfo,
                      nir_alu_type type)
{
   switch (type) {
   case nir_type_uint:
   case nir_type_uint32:
      return BRW_TYPE_UD;
   case nir_type_bool:
   case nir_type_int:
   case nir_type_bool32:
   case nir_type_int32:
      return BRW_TYPE_D;
   case nir_type_float:
   case nir_type_float32:
      return BRW_TYPE_F;
   case nir_type_float16:
      return BRW_TYPE_HF;
   case nir_type_float64:
      return BRW_TYPE_DF;
   case nir_type_int64:
      return BRW_TYPE_Q;
   case nir_type_uint64:
      return BRW_TYPE_UQ;
   case nir_type_int16:
      return BRW_TYPE_W;
   case nir_type_uint16:
      return BRW_TYPE_UW;
   case nir_type_int8:
      return BRW_TYPE_B;
   case nir_type_uint8:
      return BRW_TYPE_UB;
   default:
      UNREACHABLE("unknown type");
   }

   return BRW_TYPE_F;
}

nir_shader *
brw_nir_create_passthrough_tcs(void *mem_ctx, const struct brw_compiler *compiler,
                               const struct brw_tcs_prog_key *key)
{
   assert(key->input_vertices > 0);

   const nir_shader_compiler_options *options =
      &compiler->nir_options[MESA_SHADER_TESS_CTRL];

   uint64_t inputs_read = key->outputs_written &
      ~(VARYING_BIT_TESS_LEVEL_INNER | VARYING_BIT_TESS_LEVEL_OUTER);

   unsigned locations[64];
   unsigned num_locations = 0;

   u_foreach_bit64(varying, inputs_read)
      locations[num_locations++] = varying;

   nir_shader *nir =
      nir_create_passthrough_tcs_impl(options, locations, num_locations,
                                      key->input_vertices);

   ralloc_steal(mem_ctx, nir);

   nir->info.inputs_read = inputs_read;
   nir->info.tess._primitive_mode = key->_tes_primitive_mode;
   nir_validate_shader(nir, "in brw_nir_create_passthrough_tcs");

   struct brw_nir_compiler_opts opts = {};
   brw_preprocess_nir(compiler, nir, &opts);

   return nir;
}

nir_def *
brw_nir_load_global_const(nir_builder *b, nir_intrinsic_instr *load,
      nir_def *base_addr, unsigned off)
{
   assert(load->intrinsic == nir_intrinsic_load_push_constant ||
          load->intrinsic == nir_intrinsic_load_uniform);

   unsigned bit_size = load->def.bit_size;
   assert(bit_size >= 8 && bit_size % 8 == 0);
   nir_def *sysval;

   if (nir_src_is_const(load->src[0])) {
      uint64_t offset = off +
                        nir_intrinsic_base(load) +
                        nir_src_as_uint(load->src[0]);

      /* Things should be component-aligned. */
      assert(offset % (bit_size / 8) == 0);

      unsigned suboffset = offset % 64;
      uint64_t aligned_offset = offset - suboffset;

      /* Load two just in case we go over a 64B boundary */
      nir_def *data[2];
      for (unsigned i = 0; i < 2; i++) {
         nir_def *addr = nir_iadd_imm(b, base_addr, aligned_offset + i * 64);

         data[i] = nir_load_global_constant_uniform_block_intel(
            b, 16, 32, addr,
            .access = ACCESS_CAN_REORDER | ACCESS_NON_WRITEABLE,
            .align_mul = 64);
      }

      sysval = nir_extract_bits(b, data, 2, suboffset * 8,
                                load->num_components, bit_size);
   } else {
      nir_def *offset32 =
         nir_iadd_imm(b, load->src[0].ssa,
                         off + nir_intrinsic_base(load));
      nir_def *addr = nir_iadd(b, base_addr, nir_u2u64(b, offset32));
      sysval = nir_load_global_constant(b, load->num_components, bit_size, addr);
   }

   return sysval;
}

const struct glsl_type *
brw_nir_get_var_type(const struct nir_shader *nir, nir_variable *var)
{
   const struct glsl_type *type = var->interface_type;
   if (!type) {
      type = var->type;
      if (nir_is_arrayed_io(var, nir->info.stage)) {
         assert(glsl_type_is_array(type));
         type = glsl_get_array_element(type);
      }
   }

   return type;
}

bool
brw_nir_uses_inline_data(nir_shader *shader)
{
   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin  = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_inline_data_intel)
               continue;

            return true;
         }
      }
   }

   return false;
}

/**
 * Move load_interpolated_input with simple (payload-based) barycentric modes
 * to the top of the program so we don't emit multiple PLNs for the same input.
 *
 * This works around CSE not being able to handle non-dominating cases
 * such as:
 *
 *    if (...) {
 *       interpolate input
 *    } else {
 *       interpolate the same exact input
 *    }
 *
 * This should be replaced by global value numbering someday.
 */
bool
brw_nir_move_interpolation_to_top(nir_shader *nir)
{
   bool progress = false;

   nir_foreach_function_impl(impl, nir) {
      nir_block *top = fragment_top_block_or_after_wa_18019110168(impl);
      nir_cursor cursor = nir_before_instr(nir_block_first_instr(top));
      bool impl_progress = false;

      for (nir_block *block = nir_block_cf_tree_next(top);
           block != NULL;
           block = nir_block_cf_tree_next(block)) {

         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_interpolated_input)
               continue;
            nir_intrinsic_instr *bary_intrinsic =
               nir_def_as_intrinsic(intrin->src[0].ssa);
            nir_intrinsic_op op = bary_intrinsic->intrinsic;

            /* Leave interpolateAtSample/Offset() where they are. */
            if (op == nir_intrinsic_load_barycentric_at_sample ||
                op == nir_intrinsic_load_barycentric_at_offset)
               continue;

            nir_instr *move[3] = {
               &bary_intrinsic->instr,
               nir_def_instr(intrin->src[1].ssa),
               instr
            };

            for (unsigned i = 0; i < ARRAY_SIZE(move); i++)
               nir_instr_move(cursor, move[i]);
            impl_progress = true;
         }
      }

      progress = progress || impl_progress;

      nir_progress(impl_progress, impl, nir_metadata_control_flow);
   }

   return progress;
}

static bool
filter_simd(const nir_instr *instr, UNUSED const void *options)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   switch (nir_instr_as_intrinsic(instr)->intrinsic) {
   case nir_intrinsic_load_simd_width_intel:
   case nir_intrinsic_load_subgroup_id:
      return true;

   default:
      return false;
   }
}

static nir_def *
lower_simd(nir_builder *b, nir_instr *instr, void *options)
{
   uintptr_t simd_width = (uintptr_t)options;

   switch (nir_instr_as_intrinsic(instr)->intrinsic) {
   case nir_intrinsic_load_simd_width_intel:
      return nir_imm_int(b, simd_width);

   case nir_intrinsic_load_subgroup_id:
      /* If the whole workgroup fits in one thread, we can lower subgroup_id
       * to a constant zero.
       */
      if (!b->shader->info.workgroup_size_variable) {
         unsigned local_workgroup_size = b->shader->info.workgroup_size[0] *
                                         b->shader->info.workgroup_size[1] *
                                         b->shader->info.workgroup_size[2];
         if (local_workgroup_size <= simd_width)
            return nir_imm_int(b, 0);
      }
      return NULL;

   default:
      return NULL;
   }
}

bool
brw_nir_lower_simd(nir_shader *nir, unsigned dispatch_width)
{
   return nir_shader_lower_instructions(nir, filter_simd, lower_simd,
                                 (void *)(uintptr_t)dispatch_width);
}

nir_variable *
brw_nir_find_complete_variable_with_location(nir_shader *shader,
                                             nir_variable_mode mode,
                                             int location)
{
   nir_variable *best_var = NULL;
   unsigned last_size = 0;

   nir_foreach_variable_with_modes(var, shader, mode) {
      if (var->data.location != location)
         continue;

      unsigned new_size = glsl_count_dword_slots(var->type, false);
      if (new_size > last_size) {
         best_var = var;
         last_size = new_size;
      }
   }

   return best_var;
}

struct brw_quick_pressure_state {
   uint8_t *convergent_size;
   uint8_t *divergent_size;
   struct u_sparse_bitset live;
   unsigned curr_convergent_size;
   unsigned curr_divergent_size;
};

static inline bool
record_def_size(nir_def *def, void *v_state)
{
   struct brw_quick_pressure_state *state = v_state;

   unsigned num_components = def->num_components;

   /* Texturing has return length reduction */
   if (nir_def_is_tex(def))
      num_components = util_last_bit(nir_def_components_read(def));

   /* Assume tightly packed */
   unsigned size = DIV_ROUND_UP(num_components * def->bit_size, 32);

   nir_op alu_op =
      nir_def_is_alu(def) ?
      nir_def_as_alu(def)->op : nir_num_opcodes;

   /* Assume these are handled via source modifiers */
   if (alu_op == nir_op_fneg || alu_op == nir_op_ineg ||
       alu_op == nir_op_fabs || alu_op == nir_op_iabs)
      size = 0;

   if (nir_def_is_unused(def))
      size = 0;

   if (def->divergent) {
      state->convergent_size[def->index] = 0;
      state->divergent_size[def->index]  = size;
   } else {
      state->convergent_size[def->index] = size;
      state->divergent_size[def->index]  = 0;
   }
   return true;
}

static bool
set_src_live(nir_src *src, void *v_state)
{
   struct brw_quick_pressure_state *state = v_state;

   /* undefined variables are never live */
   if (nir_src_is_undef(*src))
      return true;

   if (!u_sparse_bitset_test(&state->live, src->ssa->index)) {
      u_sparse_bitset_set(&state->live, src->ssa->index);

      /* This value just became live, add its size */
      state->curr_convergent_size += state->convergent_size[src->ssa->index];
      state->curr_divergent_size  += state->divergent_size[src->ssa->index];
   }

   return true;
}

static bool
set_def_dead(nir_def *def, void *v_state)
{
   struct brw_quick_pressure_state *state = v_state;
   if (u_sparse_bitset_test(&state->live, def->index)) {
      u_sparse_bitset_clear(&state->live, def->index);

      /* This value just became dead, subtract its size */
      state->curr_convergent_size -= state->convergent_size[def->index];
      state->curr_divergent_size  -= state->divergent_size[def->index];
   }

   return true;
}

static void
quick_pressure_estimate(nir_shader *nir,
                        unsigned *out_convergent_size,
                        unsigned *out_divergent_size)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_metadata_require(impl, nir_metadata_divergence |
                              nir_metadata_live_defs);

   struct brw_quick_pressure_state state = {
      .convergent_size = calloc(impl->ssa_alloc, sizeof(uint8_t)),
      .divergent_size  = calloc(impl->ssa_alloc, sizeof(uint8_t)),
   };

   u_sparse_bitset_init(&state.live, impl->ssa_alloc, NULL);
   unsigned max_convergent_size = 0, max_divergent_size = 0;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_foreach_def(instr, record_def_size, &state);
      }

      state.curr_convergent_size = 0;
      state.curr_divergent_size = 0;

      /* Start with sizes for anything live-out from the block */
      U_SPARSE_BITSET_FOREACH_SET(&block->live_out, i) {
         state.curr_convergent_size += state.convergent_size[i];
         state.curr_divergent_size  += state.divergent_size[i];
      }

      /* Walk backwards, add source sizes on first sight, subtract on def */
      u_sparse_bitset_dup(&state.live, &block->live_out);

      nir_foreach_instr_reverse(instr, block) {
         if (instr->type == nir_instr_type_phi)
            break;

         nir_foreach_def(instr, set_def_dead, &state);
         nir_foreach_src(instr, set_src_live, &state);

         max_convergent_size =
            MAX2(max_convergent_size, state.curr_convergent_size);
         max_divergent_size =
            MAX2(max_divergent_size, state.curr_divergent_size);
      }
   }

   *out_convergent_size = max_convergent_size;
   *out_divergent_size  = max_divergent_size;

   free(state.convergent_size);
   free(state.divergent_size);
   u_sparse_bitset_free(&state.live);
}

/**
 * This pass performs a quick/rough estimate of register pressure in
 * SIMD8/16/32 modes, based on how many convergent and divergent values
 * exist in the SSA NIR program.  Divergent values scale up with SIMD
 * width, while convergent ones do not.
 *
 * This is fundamentally inaccurate, and can't model everything properly.
 * We try to err toward underestimating the register pressure.  The hope
 * is to use this for things like "is it worth even trying to compile a
 * SIMD<X> shader, or will it ultimately fail?"  If a lower bound on the
 * pressure is too high, we can skip all the CPU overhead from invoking
 * the backend compiler to try.  If it's close though, we'd rather say
 * to go ahead and try it rather than lose out on potential benefits of
 * larger SIMD sizes.
 */
void
brw_nir_quick_pressure_estimate(nir_shader *nir,
                                const struct intel_device_info *devinfo,
                                unsigned simd_estimate[3])
{
   unsigned convergent_size, divergent_size;
   quick_pressure_estimate(nir, &convergent_size, &divergent_size);

   /* Xe2 starts at SIMD16, rather than SIMD8 */
   simd_estimate[0] = 0;
   unsigned base_simd = devinfo->ver >= 20 ? 1 : 0;

   for (unsigned i = base_simd; i < 3; i++) {
      simd_estimate[i] = DIV_ROUND_UP(convergent_size, 8 << base_simd) +
                         divergent_size * (1 << (i - base_simd));
   }
}
