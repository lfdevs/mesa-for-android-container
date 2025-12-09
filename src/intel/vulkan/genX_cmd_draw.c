/*
 * Copyright © 2015 Intel Corporation
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

#include <assert.h>
#include <stdbool.h>

#include "anv_private.h"
#include "anv_measure.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"
#include "common/intel_genX_state_brw.h"

#include "ds/intel_tracepoints.h"

#include "genX_mi_builder.h"

static void
cmd_buffer_alloc_gfx_push_constants(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   VkShaderStageFlags stages = gfx->active_stages;

   /* In order to avoid thrash, we assume that vertex and fragment stages
    * always exist.  In the rare case where one is missing *and* the other
    * uses push concstants, this may be suboptimal.  However, avoiding stalls
    * seems more important.
    */
   stages |= VK_SHADER_STAGE_FRAGMENT_BIT;
   if (anv_gfx_has_stage(gfx, MESA_SHADER_VERTEX))
      stages |= VK_SHADER_STAGE_VERTEX_BIT;

   if (stages == cmd_buffer->state.gfx.push_constant_stages)
      return;

   unsigned push_constant_kb;

   const struct intel_device_info *devinfo = cmd_buffer->device->info;
   if (anv_gfx_has_stage(gfx, MESA_SHADER_MESH))
      push_constant_kb = devinfo->mesh_max_constant_urb_size_kb;
   else
      push_constant_kb = devinfo->max_constant_urb_size_kb;

   const unsigned num_stages =
      util_bitcount(stages & VK_SHADER_STAGE_ALL_GRAPHICS);
   unsigned size_per_stage = push_constant_kb / num_stages;

   /* Broadwell+ and Haswell gt3 require that the push constant sizes be in
    * units of 2KB.  Incidentally, these are the same platforms that have
    * 32KB worth of push constant space.
    */
   if (push_constant_kb == 32)
      size_per_stage &= ~1u;

   uint32_t kb_used = 0;
   for (int i = MESA_SHADER_VERTEX; i < MESA_SHADER_FRAGMENT; i++) {
      const unsigned push_size = (stages & (1 << i)) ? size_per_stage : 0;
      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_PUSH_CONSTANT_ALLOC_VS), alloc) {
         alloc._3DCommandSubOpcode  = 18 + i;
         alloc.ConstantBufferOffset = (push_size > 0) ? kb_used : 0;
         alloc.ConstantBufferSize   = push_size;
      }
      kb_used += push_size;
   }

   anv_batch_emit(&cmd_buffer->batch,
                  GENX(3DSTATE_PUSH_CONSTANT_ALLOC_PS), alloc) {
      alloc.ConstantBufferOffset = kb_used;
      alloc.ConstantBufferSize = push_constant_kb - kb_used;
   }

#if GFX_VERx10 == 125
   /* DG2: Wa_22011440098
    * MTL: Wa_18022330953
    *
    * In 3D mode, after programming push constant alloc command immediately
    * program push constant command(ZERO length) without any commit between
    * them.
    */
   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CONSTANT_ALL), c) {
      /* Update empty push constants for all stages (bitmask = 11111b) */
      c.ShaderUpdateEnable = 0x1f;
      c.MOCS = anv_mocs(cmd_buffer->device, NULL, 0);
   }
#endif

   cmd_buffer->state.gfx.push_constant_stages = stages;

   /* From the BDW PRM for 3DSTATE_PUSH_CONSTANT_ALLOC_VS:
    *
    *    "The 3DSTATE_CONSTANT_VS must be reprogrammed prior to
    *    the next 3DPRIMITIVE command after programming the
    *    3DSTATE_PUSH_CONSTANT_ALLOC_VS"
    *
    * Since 3DSTATE_PUSH_CONSTANT_ALLOC_VS is programmed as part of
    * pipeline setup, we need to dirty push constants.
    */
   cmd_buffer->state.push_constants_dirty |= stages;
}

static void
cmd_buffer_emit_descriptor_pointers(struct anv_cmd_buffer *cmd_buffer,
                                    uint32_t stages)
{
   static const uint32_t sampler_state_opcodes[] = {
      [MESA_SHADER_VERTEX]                      = 43,
      [MESA_SHADER_TESS_CTRL]                   = 44, /* HS */
      [MESA_SHADER_TESS_EVAL]                   = 45, /* DS */
      [MESA_SHADER_GEOMETRY]                    = 46,
      [MESA_SHADER_FRAGMENT]                    = 47,
   };

   static const uint32_t binding_table_opcodes[] = {
      [MESA_SHADER_VERTEX]                      = 38,
      [MESA_SHADER_TESS_CTRL]                   = 39,
      [MESA_SHADER_TESS_EVAL]                   = 40,
      [MESA_SHADER_GEOMETRY]                    = 41,
      [MESA_SHADER_FRAGMENT]                    = 42,
   };

   anv_foreach_stage(s, stages) {
      assert(s < ARRAY_SIZE(binding_table_opcodes));

      if (cmd_buffer->state.samplers[s].alloc_size > 0) {
         anv_batch_emit(&cmd_buffer->batch,
                        GENX(3DSTATE_SAMPLER_STATE_POINTERS_VS), ssp) {
            ssp._3DCommandSubOpcode = sampler_state_opcodes[s];
            ssp.PointertoVSSamplerState = cmd_buffer->state.samplers[s].offset;
         }
      }

      /* Always emit binding table pointers if we're asked to, since on SKL
       * this is what flushes push constants. */
      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_BINDING_TABLE_POINTERS_VS), btp) {
         btp._3DCommandSubOpcode = binding_table_opcodes[s];
         btp.PointertoVSBindingTable = cmd_buffer->state.binding_tables[s].offset;
      }
   }
}

static struct anv_address
get_push_range_address(struct anv_cmd_buffer *cmd_buffer,
                       const struct anv_shader *shader,
                       const struct anv_push_range *range)
{
   struct anv_cmd_graphics_state *gfx_state = &cmd_buffer->state.gfx;
   switch (range->set) {
   case ANV_DESCRIPTOR_SET_DESCRIPTORS: {
      /* This is a descriptor set buffer so the set index is
       * actually given by binding->binding.  (Yes, that's
       * confusing.)
       */
      struct anv_descriptor_set *set =
         gfx_state->base.descriptors[range->index];
      return anv_descriptor_set_address(set);
   }

   case ANV_DESCRIPTOR_SET_DESCRIPTORS_BUFFER: {
      return anv_address_from_u64(
         anv_cmd_buffer_descriptor_buffer_address(
            cmd_buffer,
            gfx_state->base.descriptor_buffers[range->index].buffer_index) +
         gfx_state->base.descriptor_buffers[range->index].buffer_offset);
   }

   case ANV_DESCRIPTOR_SET_PUSH_CONSTANTS: {
      if (gfx_state->base.push_constants_state.alloc_size == 0) {
         gfx_state->base.push_constants_state =
            anv_cmd_buffer_gfx_push_constants(cmd_buffer);
      }
      return anv_cmd_buffer_gfx_push_constants_state_address(
         cmd_buffer, gfx_state->base.push_constants_state);
   }

   case ANV_DESCRIPTOR_SET_NULL:
   case ANV_DESCRIPTOR_SET_PER_PRIM_PADDING:
      return cmd_buffer->device->workaround_address;

   default: {
      assert(range->set < MAX_SETS);
      struct anv_descriptor_set *set =
         gfx_state->base.descriptors[range->set];
      const struct anv_descriptor *desc =
         &set->descriptors[range->index];

      if (desc->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
         if (desc->buffer) {
            return anv_address_add(desc->buffer->address,
                                   desc->offset);
         }
      } else {
         assert(desc->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
         if (desc->buffer) {
            const struct anv_cmd_pipeline_state *pipe_state = &gfx_state->base;
            uint32_t dynamic_offset =
               pipe_state->dynamic_offsets[
                  range->set].offsets[range->dynamic_offset_index];
            return anv_address_add(desc->buffer->address,
                                   desc->offset + dynamic_offset);
         }
      }

      /* For NULL UBOs, we just return an address in the workaround BO.  We do
       * writes to it for workarounds but always at the bottom.  The higher
       * bytes should be all zeros.
       */
      assert(range->length * 32 <= 2048);
      return cmd_buffer->device->workaround_address;
   }
   }
}


/** Returns the size in bytes of the bound buffer
 *
 * The range is relative to the start of the buffer, not the start of the
 * range.  The returned range may be smaller than
 *
 *    (range->start + range->length) * 32;
 */
static uint32_t
get_push_range_bound_size(struct anv_cmd_buffer *cmd_buffer,
                          const struct anv_shader *shader,
                          const struct anv_push_range *range)
{
   assert(shader->vk.stage != MESA_SHADER_COMPUTE);
   const struct anv_cmd_graphics_state *gfx_state = &cmd_buffer->state.gfx;
   switch (range->set) {
   case ANV_DESCRIPTOR_SET_DESCRIPTORS: {
      struct anv_descriptor_set *set =
         gfx_state->base.descriptors[range->index];
      struct anv_state state = set->desc_surface_mem;
      assert(range->start * 32 < state.alloc_size);
      assert((range->start + range->length) * 32 <= state.alloc_size);
      return state.alloc_size;
   }

   case ANV_DESCRIPTOR_SET_DESCRIPTORS_BUFFER:
      /* It's hard to bound a reference to a descriptor buffer because we
       * don't have an actual buffer, only an address. So just return the
       * maximum size of the heap (which bounds the largest buffer size).
       */
      return anv_physical_device_bindless_heap_size(
         cmd_buffer->device->physical, true);

   case ANV_DESCRIPTOR_SET_NULL:
   case ANV_DESCRIPTOR_SET_PUSH_CONSTANTS:
   case ANV_DESCRIPTOR_SET_PER_PRIM_PADDING:
      return (range->start + range->length) * 32;

   default: {
      assert(range->set < MAX_SETS);
      struct anv_descriptor_set *set =
         gfx_state->base.descriptors[range->set];
      const struct anv_descriptor *desc =
         &set->descriptors[range->index];

      if (desc->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
         /* Here we promote a UBO to a binding table entry so that we can avoid a layer of indirection.
            * We use the descriptor set's internally allocated surface state to fill the binding table entry.
         */
         if (!desc->buffer)
            return 0;

         if (range->start * 32 > desc->bind_range)
            return 0;

         return desc->bind_range;
      } else {
         if (!desc->buffer)
            return 0;

         assert(desc->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
         /* Compute the offset within the buffer */
         const struct anv_cmd_pipeline_state *pipe_state = &gfx_state->base;
         uint32_t dynamic_offset =
            pipe_state->dynamic_offsets[
               range->set].offsets[range->dynamic_offset_index];
         uint64_t offset = desc->offset + dynamic_offset;
         /* Clamp to the buffer size */
         offset = MIN2(offset, desc->buffer->vk.size);
         /* Clamp the range to the buffer size */
         uint32_t bound_range = MIN2(desc->range, desc->buffer->vk.size - offset);

         /* Align the range for consistency */
         bound_range = align(bound_range, ANV_UBO_BOUNDS_CHECK_ALIGNMENT);

         return bound_range;
      }
   }
   }
}

static void
cmd_buffer_emit_push_constant(struct anv_cmd_buffer *cmd_buffer,
                              mesa_shader_stage stage,
                              struct anv_address *buffers,
                              unsigned buffer_count)
{
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   static const uint32_t push_constant_opcodes[] = {
      [MESA_SHADER_VERTEX]                      = 21,
      [MESA_SHADER_TESS_CTRL]                   = 25, /* HS */
      [MESA_SHADER_TESS_EVAL]                   = 26, /* DS */
      [MESA_SHADER_GEOMETRY]                    = 22,
      [MESA_SHADER_FRAGMENT]                    = 23,
   };

   assert(stage < ARRAY_SIZE(push_constant_opcodes));

   UNUSED uint32_t mocs = anv_mocs(cmd_buffer->device, NULL, 0);

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CONSTANT_VS), c) {
      c._3DCommandSubOpcode = push_constant_opcodes[stage];

      /* Set MOCS.
       *
       * We only have one MOCS field for the whole packet, not one per
       * buffer.  We could go out of our way here to walk over all of
       * the buffers and see if any of them are used externally and use
       * the external MOCS.  However, the notion that someone would use
       * the same bit of memory for both scanout and a UBO is nuts.
       *
       * Let's not bother and assume it's all internal.
       */
      c.MOCS = mocs;

      if (anv_gfx_has_stage(gfx, stage)) {
         const struct anv_pipeline_bind_map *bind_map =
            &gfx->shaders[stage]->bind_map;

         /* The Skylake PRM contains the following restriction:
          *
          *    "The driver must ensure The following case does not occur
          *     without a flush to the 3D engine: 3DSTATE_CONSTANT_* with
          *     buffer 3 read length equal to zero committed followed by a
          *     3DSTATE_CONSTANT_* with buffer 0 read length not equal to
          *     zero committed."
          *
          * To avoid this, we program the buffers in the highest slots.
          * This way, slot 0 is only used if slot 3 is also used.
          */
         assert(buffer_count <= 4);
         const unsigned shift = 4 - buffer_count;
         for (unsigned i = 0; i < buffer_count; i++) {
            const struct anv_push_range *range = &bind_map->push_ranges[i];

            /* At this point we only have non-empty ranges */
            assert(range->length > 0);

            c.ConstantBody.ReadLength[i + shift] = range->length;
            c.ConstantBody.Buffer[i + shift] =
               anv_address_add(buffers[i], range->start * 32);
         }
      }
   }
}

#if GFX_VER >= 12
static void
cmd_buffer_emit_push_constant_all(struct anv_cmd_buffer *cmd_buffer,
                                  uint32_t shader_mask,
                                  struct anv_address *buffers,
                                  uint32_t buffer_count)
{
   if (buffer_count == 0) {
      if (shader_mask) {
         anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_CONSTANT_ALL), c) {
            c.ShaderUpdateEnable = shader_mask;
            c.MOCS = isl_mocs(&cmd_buffer->device->isl_dev, 0, false);
         }
      }

      return;
   }

   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   mesa_shader_stage stage = vk_to_mesa_shader_stage(shader_mask);

   const struct anv_pipeline_bind_map *bind_map =
      &gfx->shaders[stage]->bind_map;

   uint32_t *dw;
   const uint32_t buffer_mask = (1 << buffer_count) - 1;
   const uint32_t num_dwords = 2 + 2 * buffer_count;

   dw = anv_batch_emitn(&cmd_buffer->batch, num_dwords,
                        GENX(3DSTATE_CONSTANT_ALL),
                        .ShaderUpdateEnable = shader_mask,
                        .PointerBufferMask = buffer_mask,
                        .MOCS = isl_mocs(&cmd_buffer->device->isl_dev, 0, false));

   for (int i = 0; i < buffer_count; i++) {
      const struct anv_push_range *range = &bind_map->push_ranges[i];
      GENX(3DSTATE_CONSTANT_ALL_DATA_pack)(
         &cmd_buffer->batch, dw + 2 + i * 2,
         &(struct GENX(3DSTATE_CONSTANT_ALL_DATA)) {
            .PointerToConstantBuffer =
               anv_address_add(buffers[i], range->start * 32),
            .ConstantBufferReadLength = range->length,
         });
   }
}
#endif

static void
cmd_buffer_flush_gfx_push_constants(struct anv_cmd_buffer *cmd_buffer,
                                    VkShaderStageFlags dirty_stages)
{
   VkShaderStageFlags flushed = 0;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

#if GFX_VER >= 12
   uint32_t nobuffer_stages = 0;
#endif

   /* Compute robust pushed register access mask for each stage. */
   anv_foreach_stage(stage, dirty_stages) {
      if (!anv_gfx_has_stage(gfx, stage))
         continue;

      const struct anv_shader *shader = gfx->shaders[stage];
      if (shader->prog_data->robust_ubo_ranges) {
         const struct anv_pipeline_bind_map *bind_map = &shader->bind_map;
         struct anv_push_constants *push = &gfx->base.push_constants;

         unsigned ubo_range_index = 0;
         for (unsigned i = 0; i < 4; i++) {
            const struct anv_push_range *range = &bind_map->push_ranges[i];
            if (range->length == 0)
               continue;

            /* Skip any push ranges that were not promoted from UBOs */
            if (range->set >= MAX_SETS) {
               /* The indexing in prog_data->robust_ubo_ranges is based off
                * prog_data->ubo_ranges which does not include the
                * prog_data->nr_params (Vulkan push constants).
                */
               if (range->set != ANV_DESCRIPTOR_SET_PUSH_CONSTANTS)
                  ubo_range_index++;
               continue;
            }

            assert(shader->prog_data->robust_ubo_ranges & (1 << ubo_range_index));

            unsigned bound_size =
               get_push_range_bound_size(cmd_buffer, shader, range);

            uint8_t range_mask = 0;

            /* Determine the bound length of the range in 16-byte units */
            if (bound_size > range->start * 32) {
               bound_size = MIN2(
                  DIV_ROUND_UP(bound_size - range->start * 32, 16),
                  2 * range->length);
               range_mask = (uint8_t) bound_size;
               assert(bound_size < 256);
            }

            /* Update the pushed bound length constant if it changed */
            if (range_mask != push->gfx.push_reg_mask[stage][ubo_range_index]) {
               push->gfx.push_reg_mask[stage][ubo_range_index] = range_mask;
               cmd_buffer->state.push_constants_dirty |=
                  mesa_to_vk_shader_stage(stage);
               gfx->base.push_constants_data_dirty = true;
            }

            ubo_range_index++;
         }
      }
   }

   /* Setting NULL resets the push constant state so that we allocate a new one
    * if needed. If push constant data not dirty, get_push_range_address can
    * re-use existing allocation.
    *
    * Always reallocate on gfx9, gfx11 to fix push constant related flaky tests.
    * See https://gitlab.freedesktop.org/mesa/mesa/-/issues/11064
    */
   if (gfx->base.push_constants_data_dirty || GFX_VER < 12)
      gfx->base.push_constants_state = ANV_STATE_NULL;

#if GFX_VERx10 >= 125
   const struct brw_mesh_prog_data *mesh_prog_data =
      get_gfx_mesh_prog_data(gfx);
#endif

   anv_foreach_stage(stage, dirty_stages) {
      unsigned buffer_count = 0;
      flushed |= mesa_to_vk_shader_stage(stage);
      UNUSED uint32_t max_push_range = 0;

      struct anv_address buffers[4] = {};
      if (anv_gfx_has_stage(gfx, stage)) {
         const struct anv_shader *shader = gfx->shaders[stage];
         const struct anv_pipeline_bind_map *bind_map = &shader->bind_map;

         /* We have to gather buffer addresses as a second step because the
          * loop above puts data into the push constant area and the call to
          * get_push_range_address is what locks our push constants and copies
          * them into the actual GPU buffer.  If we did the two loops at the
          * same time, we'd risk only having some of the sizes in the push
          * constant buffer when we did the copy.
          */
         for (unsigned i = 0; i < 4; i++) {
            const struct anv_push_range *range = &bind_map->push_ranges[i];
            if (range->length == 0)
               break;

#if GFX_VERx10 >= 125
            /* Padding for Mesh only matters where the platform supports Mesh
             * shaders.
             */
            if (range->set == ANV_DESCRIPTOR_SET_PER_PRIM_PADDING &&
                mesh_prog_data && !mesh_prog_data->map.wa_18019110168_active) {
               break;
            }
#endif

            buffers[i] = get_push_range_address(cmd_buffer, shader, range);
            max_push_range = MAX2(max_push_range, range->length);
            buffer_count++;
         }

         /* We have at most 4 buffers but they should be tightly packed */
         for (unsigned i = buffer_count; i < 4; i++) {
            assert(bind_map->push_ranges[i].length == 0 ||
                   bind_map->push_ranges[i].set ==
                   ANV_DESCRIPTOR_SET_PER_PRIM_PADDING);
         }
      }

#if GFX_VER >= 12
      /* If this stage doesn't have any push constants, emit it later in a
       * single CONSTANT_ALL packet.
       */
      if (buffer_count == 0) {
         nobuffer_stages |= 1 << stage;
         continue;
      }

      /* The Constant Buffer Read Length field from 3DSTATE_CONSTANT_ALL
       * contains only 5 bits, so we can only use it for buffers smaller than
       * 32.
       *
       * According to Wa_16011448509, Gfx12.0 misinterprets some address bits
       * in 3DSTATE_CONSTANT_ALL.  It should still be safe to use the command
       * for disabling stages, where all address bits are zero.  However, we
       * can't safely use it for general buffers with arbitrary addresses.
       * Just fall back to the individual 3DSTATE_CONSTANT_XS commands in that
       * case.
       */
      if (max_push_range < 32 && GFX_VERx10 > 120) {
         cmd_buffer_emit_push_constant_all(cmd_buffer, 1 << stage,
                                           buffers, buffer_count);
         continue;
      }
#endif

      cmd_buffer_emit_push_constant(cmd_buffer, stage, buffers, buffer_count);
   }

#if GFX_VER >= 12
   if (nobuffer_stages)
      /* Wa_16011448509: all address bits are zero */
      cmd_buffer_emit_push_constant_all(cmd_buffer, nobuffer_stages, NULL, 0);
#endif

   cmd_buffer->state.push_constants_dirty &= ~flushed;
   gfx->base.push_constants_data_dirty = false;
}

#if GFX_VERx10 >= 125
static inline uint64_t
get_mesh_task_push_addr64(struct anv_cmd_buffer *cmd_buffer,
                          struct anv_cmd_graphics_state *gfx,
                          mesa_shader_stage stage)
{
   const struct anv_shader *shader = gfx->shaders[stage];
   const struct anv_pipeline_bind_map *bind_map = &shader->bind_map;
   if (bind_map->push_ranges[0].length == 0)
      return 0;

   if (gfx->base.push_constants_state.alloc_size == 0) {
      gfx->base.push_constants_state =
         anv_cmd_buffer_gfx_push_constants(cmd_buffer);
   }

   return anv_address_physical(
      anv_address_add(
         anv_cmd_buffer_gfx_push_constants_state_address(
            cmd_buffer, gfx->base.push_constants_state),
         bind_map->push_ranges[0].start * 32));
}

static void
cmd_buffer_flush_mesh_inline_data(struct anv_cmd_buffer *cmd_buffer,
                                  VkShaderStageFlags dirty_stages)
{
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (dirty_stages & VK_SHADER_STAGE_TASK_BIT_EXT &&
       anv_gfx_has_stage(gfx, MESA_SHADER_TASK)) {
      uint64_t push_addr64 =
         get_mesh_task_push_addr64(cmd_buffer, gfx, MESA_SHADER_TASK);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_TASK_SHADER_DATA), data) {
         data.InlineData[ANV_INLINE_PARAM_PUSH_ADDRESS_OFFSET / 4 + 0] = push_addr64 & 0xffffffff;
         data.InlineData[ANV_INLINE_PARAM_PUSH_ADDRESS_OFFSET / 4 + 1] = push_addr64 >> 32;
      }
   }

   if (dirty_stages & VK_SHADER_STAGE_MESH_BIT_EXT &&
       anv_gfx_has_stage(gfx, MESA_SHADER_MESH)) {
      uint64_t push_addr64 =
         get_mesh_task_push_addr64(cmd_buffer, gfx, MESA_SHADER_MESH);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_MESH_SHADER_DATA), data) {
         data.InlineData[ANV_INLINE_PARAM_PUSH_ADDRESS_OFFSET / 4 + 0] = push_addr64 & 0xffffffff;
         data.InlineData[ANV_INLINE_PARAM_PUSH_ADDRESS_OFFSET / 4 + 1] = push_addr64 >> 32;
         data.InlineData[ANV_INLINE_PARAM_MESH_PROVOKING_VERTEX / 4]   = gfx->dyn_state.mesh_provoking_vertex;
      }
   }

   cmd_buffer->state.push_constants_dirty &= ~dirty_stages;
}
#endif

ALWAYS_INLINE static void
cmd_buffer_maybe_flush_rt_writes(struct anv_cmd_buffer *cmd_buffer,
                                 struct anv_cmd_graphics_state *gfx,
                                 const struct vk_dynamic_graphics_state *dyn)
{
   if (!anv_gfx_has_stage(gfx, MESA_SHADER_FRAGMENT))
      return;

   /* Count the number of color attachments in the binding table */
   const struct anv_pipeline_bind_map *bind_map =
      &gfx->shaders[MESA_SHADER_FRAGMENT]->bind_map;

   /* Build a map of fragment color output to attachment */
   uint8_t rt_to_att[MAX_RTS];
   memset(rt_to_att, ANV_COLOR_OUTPUT_DISABLED, MAX_RTS);
   for (uint32_t i = 0; i < gfx->color_att_count; i++) {
      if (dyn->cal.color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
         rt_to_att[dyn->cal.color_map[i]] = i;
   }

   /* For each fragment shader output if not unused apply the remapping to
    * pipeline->color_output_mapping
    */
   UNUSED bool need_rt_flush = false;
   for (unsigned rt = 0; rt < MIN2(bind_map->surface_count, MAX_RTS); rt++) {
      if (bind_map->surface_to_descriptor[rt].set !=
          ANV_DESCRIPTOR_SET_COLOR_ATTACHMENTS)
         break;

      uint32_t index = bind_map->surface_to_descriptor[rt].index;
      if (index == ANV_COLOR_OUTPUT_UNUSED)
         continue;

      if (index == ANV_COLOR_OUTPUT_DISABLED &&
          gfx->color_output_mapping[rt] != index) {
         gfx->color_output_mapping[rt] = index;
         need_rt_flush = true;
      } else if (gfx->color_output_mapping[rt] != rt_to_att[rt])  {
         gfx->color_output_mapping[rt] = rt_to_att[rt];
         need_rt_flush = true;
      }
   }

   if (need_rt_flush) {
      cmd_buffer->state.descriptors_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;
#if GFX_VER >= 11
      /* The PIPE_CONTROL command description says:
       *
       *    "Whenever a Binding Table Index (BTI) used by a Render Target Message
       *     points to a different RENDER_SURFACE_STATE, SW must issue a Render
       *     Target Cache Flush by enabling this bit. When render target flush
       *     is set due to new association of BTI, PS Scoreboard Stall bit must
       *     be set in this packet."
       *
       * Within a renderpass, the render target entries in the binding tables
       * remain the same as what was setup at CmdBeginRendering() with one
       * exception where have to setup a null render target because a fragment
       * writes only depth/stencil yet the renderpass has been setup with at
       * least one color attachment. This is because our render target messages
       * in the shader always send the color.
       */
      anv_add_pending_pipe_bits(cmd_buffer,
                                ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT |
                                ANV_PIPE_STALL_AT_SCOREBOARD_BIT,
                                "change RT due to shader outputs");
#endif
   }
}

ALWAYS_INLINE static void
cmd_buffer_flush_vertex_buffers(struct anv_cmd_buffer *cmd_buffer,
                                uint32_t vb_emit)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;
   const uint32_t num_buffers = __builtin_popcount(vb_emit);
   const uint32_t num_dwords = 1 + num_buffers * 4;
   uint32_t *p = anv_batch_emitn(&cmd_buffer->batch, num_dwords,
                                 GENX(3DSTATE_VERTEX_BUFFERS));
   uint32_t i = 0;
   u_foreach_bit(vb, vb_emit) {
      const struct anv_vertex_binding *binding =
         &cmd_buffer->state.vertex_bindings[vb];

      struct GENX(VERTEX_BUFFER_STATE) state;
      if (binding->size > 0) {
         uint32_t stride = dyn->vi_binding_strides[vb];

         state = (struct GENX(VERTEX_BUFFER_STATE)) {
            .VertexBufferIndex = vb,

            .MOCS = binding->mocs,
            .AddressModifyEnable = true,
            .BufferPitch = stride,
            .BufferStartingAddress = anv_address_from_u64(binding->addr),
#if GFX_VER >= 12
            .L3BypassDisable = true,
#endif

            .BufferSize = binding->size,
         };
      } else {
         state = (struct GENX(VERTEX_BUFFER_STATE)) {
            .VertexBufferIndex = vb,
            .NullVertexBuffer = true,
            .MOCS = anv_mocs(cmd_buffer->device, NULL,
                             ISL_SURF_USAGE_VERTEX_BUFFER_BIT),
         };
      }

#if GFX_VER == 9
      genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(cmd_buffer, vb,
                                                     state.BufferStartingAddress,
                                                     state.BufferSize);
#endif

      GENX(VERTEX_BUFFER_STATE_pack)(&cmd_buffer->batch, &p[1 + i * 4], &state);
      i++;
   }
}

ALWAYS_INLINE static void
cmd_buffer_flush_gfx_state(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct vk_dynamic_graphics_state *dyn =
      &cmd_buffer->vk.dynamic_graphics_state;

   assert((gfx->active_stages & VK_SHADER_STAGE_COMPUTE_BIT) == 0);

   genX(cmd_buffer_config_l3)(cmd_buffer, device->l3_config);

   genX(cmd_buffer_update_color_aux_op(cmd_buffer, ISL_AUX_OP_NONE));

   genX(cmd_buffer_emit_hashing_mode)(cmd_buffer, UINT_MAX, UINT_MAX, 1);

   genX(flush_descriptor_buffers)(cmd_buffer, &gfx->base, gfx->active_stages);

   genX(flush_pipeline_select_3d)(cmd_buffer);

   if (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PRERASTER_SHADERS) {
      /* Wa_14015814527
       *
       * Apply task URB workaround when switching from task to primitive.
       */
      if (!anv_gfx_has_stage(gfx, MESA_SHADER_MESH)) {
         genX(apply_task_urb_workaround)(cmd_buffer);
      } else if (anv_gfx_has_stage(gfx, MESA_SHADER_TASK)) {
         cmd_buffer->state.gfx.used_task_shader = true;
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_COLOR_ATTACHMENT_MAP) ||
       (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PS))
      cmd_buffer_maybe_flush_rt_writes(cmd_buffer, gfx, dyn);

   /* Apply any pending pipeline flushes we may have.  We want to apply them
    * now because, if any of those flushes are for things like push constants,
    * the GPU will read the state at weird times.
    */
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   /* Check what vertex buffers have been rebound against the set of bindings
    * being used by the current set of vertex attributes.
    */
   uint32_t vb_emit = cmd_buffer->state.gfx.vb_dirty & dyn->vi->bindings_valid;
   /* If the pipeline changed, the we have to consider all the valid bindings. */
   if ((cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_VS) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDINGS_VALID) ||
       BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDING_STRIDES))
      vb_emit |= dyn->vi->bindings_valid;

   if (vb_emit) {
      cmd_buffer_flush_vertex_buffers(cmd_buffer, vb_emit);
      cmd_buffer->state.gfx.vb_dirty &= ~vb_emit;
   }

   const bool any_dynamic_state_dirty =
      vk_dynamic_graphics_state_any_dirty(dyn);
   uint32_t descriptors_dirty = cmd_buffer->state.descriptors_dirty &
                                gfx->active_stages;

   descriptors_dirty |=
      genX(cmd_buffer_flush_push_descriptors)(cmd_buffer,
                                              &cmd_buffer->state.gfx.base);

   if (!cmd_buffer->state.gfx.dirty && !descriptors_dirty &&
       !any_dynamic_state_dirty &&
       ((cmd_buffer->state.push_constants_dirty &
         (VK_SHADER_STAGE_ALL_GRAPHICS |
          VK_SHADER_STAGE_TASK_BIT_EXT |
          VK_SHADER_STAGE_MESH_BIT_EXT)) == 0))
      return;

   if (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_XFB_ENABLE) {
      /* Wa_16011411144:
       *
       * SW must insert a PIPE_CONTROL cmd before and after the
       * 3dstate_so_buffer_index_0/1/2/3 states to ensure so_buffer_index_*
       * state is not combined with other state changes.
       */
      if (intel_needs_workaround(device->info, 16011411144)) {
         anv_add_pending_pipe_bits(cmd_buffer,
                                   ANV_PIPE_CS_STALL_BIT,
                                   "before SO_BUFFER change WA");
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
      }

      /* We don't need any per-buffer dirty tracking because you're not
       * allowed to bind different XFB buffers while XFB is enabled.
       */
      for (unsigned idx = 0; idx < MAX_XFB_BUFFERS; idx++) {
         struct anv_xfb_binding *xfb = &cmd_buffer->state.xfb_bindings[idx];
         anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_SO_BUFFER), sob) {
#if GFX_VER < 12
            sob.SOBufferIndex = idx;
#else
            sob._3DCommandOpcode = 0;
            sob._3DCommandSubOpcode = SO_BUFFER_INDEX_0_CMD + idx;
#endif

            if (cmd_buffer->state.xfb_enabled &&
                xfb->addr != 0 && xfb->size != 0) {
               sob.MOCS = xfb->mocs;
               sob.SurfaceBaseAddress = anv_address_from_u64(xfb->addr);
               sob.SOBufferEnable = true;
               sob.StreamOffsetWriteEnable = false;
               /* Size is in DWords - 1 */
               sob.SurfaceSize = DIV_ROUND_UP(xfb->size, 4) - 1;
            } else {
               sob.MOCS = anv_mocs(device, NULL, 0);
            }
         }
      }

      if (intel_needs_workaround(device->info, 16011411144)) {
         /* Wa_16011411144: also CS_STALL after touching SO_BUFFER change */
         anv_add_pending_pipe_bits(cmd_buffer,
                                   ANV_PIPE_CS_STALL_BIT,
                                   "after SO_BUFFER change WA");
         genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
      } else if (GFX_VER >= 10) {
         /* CNL and later require a CS stall after 3DSTATE_SO_BUFFER */
         anv_add_pending_pipe_bits(cmd_buffer,
                                   ANV_PIPE_CS_STALL_BIT,
                                   "after 3DSTATE_SO_BUFFER call");
      }
   }

   /* Flush the runtime state into the HW state tracking */
   if (cmd_buffer->state.gfx.dirty || any_dynamic_state_dirty)
      genX(cmd_buffer_flush_gfx_runtime_state)(cmd_buffer);

   /* Flush the HW state into the commmand buffer */
   if (!BITSET_IS_EMPTY(cmd_buffer->state.gfx.dyn_state.emit_dirty))
      genX(cmd_buffer_flush_gfx_hw_state)(cmd_buffer);

   /* If the pipeline changed, we may need to re-allocate push constant space
    * in the URB.
    */
   if (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_PUSH_CONSTANT_SHADERS)
      cmd_buffer_alloc_gfx_push_constants(cmd_buffer);

#if GFX_VERx10 < 125
   if (cmd_buffer->state.gfx.dirty & (ANV_CMD_DIRTY_VS |
                                      ANV_CMD_DIRTY_HS |
                                      ANV_CMD_DIRTY_DS |
                                      ANV_CMD_DIRTY_GS |
                                      ANV_CMD_DIRTY_PS)) {
      for (unsigned s = 0; s <= MESA_SHADER_FRAGMENT; s++) {
         if (gfx->shaders[s] == NULL)
            continue;

         /* Also add the relocations (scratch buffers) */
         VkResult result = anv_reloc_list_append(cmd_buffer->batch.relocs,
                                                 &gfx->shaders[s]->relocs);
         if (result != VK_SUCCESS) {
            anv_batch_set_error(&cmd_buffer->batch, result);
            return;
         }
      }
   }
#endif

   /* Render targets live in the same binding table as fragment descriptors */
   if (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_RENDER_TARGETS)
      descriptors_dirty |= VK_SHADER_STAGE_FRAGMENT_BIT;

   /* We emit the binding tables and sampler tables first, then emit push
    * constants and then finally emit binding table and sampler table
    * pointers.  It has to happen in this order, since emitting the binding
    * tables may change the push constants (in case of storage images). After
    * emitting push constants, on SKL+ we have to emit the corresponding
    * 3DSTATE_BINDING_TABLE_POINTER_* for the push constants to take effect.
    */
   uint32_t dirty = 0;
   if (descriptors_dirty) {
      dirty = genX(cmd_buffer_flush_descriptor_sets)(
         cmd_buffer,
         &cmd_buffer->state.gfx.base,
         descriptors_dirty,
         (const struct anv_shader **)gfx->shaders,
         ARRAY_SIZE(gfx->shaders));
      cmd_buffer->state.descriptors_dirty &= ~dirty;
   }

   if (dirty || cmd_buffer->state.push_constants_dirty) {
      /* Because we're pushing UBOs, we have to push whenever either
       * descriptors or push constants is dirty.
       */
      dirty |= cmd_buffer->state.push_constants_dirty & gfx->active_stages;
#if INTEL_NEEDS_WA_1604061319
      /* Testing shows that all the 3DSTATE_CONSTANT_XS need to be emitted if
       * any stage has 3DSTATE_CONSTANT_XS emitted.
       */
      dirty |= gfx->active_stages;
#endif
      cmd_buffer_flush_gfx_push_constants(cmd_buffer,
                                          dirty & VK_SHADER_STAGE_ALL_GRAPHICS);
#if GFX_VERx10 >= 125
      cmd_buffer_flush_mesh_inline_data(
         cmd_buffer, dirty & (VK_SHADER_STAGE_TASK_BIT_EXT |
                              VK_SHADER_STAGE_MESH_BIT_EXT));
#endif
   }

   if (dirty & VK_SHADER_STAGE_ALL_GRAPHICS) {
      cmd_buffer_emit_descriptor_pointers(cmd_buffer,
                                          dirty & VK_SHADER_STAGE_ALL_GRAPHICS);
   }

#if GFX_VER >= 20
   if (cmd_buffer->state.gfx.dirty & ANV_CMD_DIRTY_INDIRECT_DATA_STRIDE) {
      anv_batch_emit(&cmd_buffer->batch, GENX(STATE_BYTE_STRIDE), sb_stride) {
         sb_stride.ByteStride = cmd_buffer->state.gfx.indirect_data_stride >> 2;
         sb_stride.ByteStrideEnable =
            cmd_buffer->state.gfx.indirect_data_stride_aligned == U_TRISTATE_NO;
      }
   }
#endif

   cmd_buffer->state.gfx.dirty = 0;
}

void
genX(cmd_buffer_flush_gfx_state)(struct anv_cmd_buffer *cmd_buffer)
{
   cmd_buffer_flush_gfx_state(cmd_buffer);
}

ALWAYS_INLINE static bool
anv_use_generated_draws(const struct anv_cmd_buffer *cmd_buffer, uint32_t count)
{
   const struct anv_device *device = cmd_buffer->device;

   /* We cannot generate readable commands in protected mode. */
   if (cmd_buffer->vk.pool->flags & VK_COMMAND_POOL_CREATE_PROTECTED_BIT)
      return false;

   /* Limit generated draws to pipelines without HS stage. This makes things
    * simpler for implementing Wa_1306463417, Wa_16011107343.
    */
   if ((INTEL_NEEDS_WA_1306463417 || INTEL_NEEDS_WA_16011107343) &&
       anv_gfx_has_stage(&cmd_buffer->state.gfx, MESA_SHADER_TESS_CTRL))
      return false;

   return count >= device->physical->instance->generated_indirect_threshold;
}

#include "genX_cmd_draw_helpers.h"
#include "genX_cmd_draw_generated_indirect.h"

ALWAYS_INLINE static void
cmd_buffer_pre_draw_wa(struct anv_cmd_buffer *cmd_buffer)
{
   UNUSED const struct anv_device *device = cmd_buffer->device;
   UNUSED const struct anv_instance *instance =
      device->physical->instance;
   UNUSED const bool protected = cmd_buffer->vk.pool->flags &
                                 VK_COMMAND_POOL_CREATE_PROTECTED_BIT;
   UNUSED struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   UNUSED struct anv_gfx_dynamic_state *hw_state = &gfx->dyn_state;

   struct mi_builder b;
   if (unlikely(instance->debug & ANV_DEBUG_SHADER_HASH)) {
      mi_builder_init(&b, device->info, &cmd_buffer->batch);
      mi_builder_set_mocs(&b, isl_mocs(&device->isl_dev, 0, false));
   }

#define DEBUG_SHADER_HASH(stage) do {                                   \
      if (unlikely(instance->debug & ANV_DEBUG_SHADER_HASH)) {          \
         mi_store(&b,                                                   \
                  mi_mem32(device->workaround_address),                 \
                  mi_imm(gfx->shaders[stage]->prog_data->source_hash)); \
      }                                                                 \
   } while (0)

#define anv_batch_emit_gfx(batch, cmd, name) ({                         \
      void *__dst = anv_batch_emit_dwords(                              \
         batch, __anv_cmd_length(cmd));                                 \
      memcpy(__dst, hw_state->packed.name,                              \
             4 * __anv_cmd_length(cmd));                                \
      VG(VALGRIND_CHECK_MEM_IS_DEFINED(                                 \
            __dst, __anv_cmd_length(cmd) * 4));                         \
      __dst;                                                            \
   })

#if INTEL_WA_16011107343_GFX_VER
   if (intel_needs_workaround(cmd_buffer->device->info, 16011107343) &&
       anv_gfx_has_stage(gfx, MESA_SHADER_TESS_CTRL)) {
      DEBUG_SHADER_HASH(MESA_SHADER_TESS_CTRL);
      anv_batch_emit_gfx(&cmd_buffer->batch, GENX(3DSTATE_HS), hs);
   }
#endif

#if INTEL_WA_22018402687_GFX_VER
   if (intel_needs_workaround(cmd_buffer->device->info, 22018402687) &&
       anv_gfx_has_stage(gfx, MESA_SHADER_TESS_EVAL)) {
      DEBUG_SHADER_HASH(MESA_SHADER_TESS_EVAL);
      /* Wa_22018402687:
       *   In any 3D enabled context, just before any Tessellation enabled
       *   draw call (3D Primitive), re-send the last programmed 3DSTATE_DS
       *   again. This will make sure that the 3DSTATE_INT generated just
       *   before the draw call will have TDS dirty which will make sure TDS
       *   will launch the state thread before the draw call.
       *
       * This fixes a hang resulting from running anything using tessellation
       * after a switch away from the mesh pipeline. We don't need to track
       * said switch, as it matters at the HW level, and can be triggered even
       * across processes, so we apply the Wa at all times.
       */
      anv_batch_emit_gfx(&cmd_buffer->batch, GENX(3DSTATE_DS), ds);
   }
#endif

   genX(emit_breakpoint)(&cmd_buffer->batch, cmd_buffer->device, true);

#undef anv_batch_emit_gfx
#undef DEBUG_SHADER_HASH
}

ALWAYS_INLINE static void
batch_post_draw_wa(struct anv_batch *batch,
                   const struct anv_device *device,
                   uint32_t primitive_topology,
                   uint32_t vertex_count)
{
#if INTEL_WA_22014412737_GFX_VER || INTEL_WA_16014538804_GFX_VER
   if (intel_needs_workaround(device->info, 22014412737) &&
       (primitive_topology == _3DPRIM_POINTLIST ||
        primitive_topology == _3DPRIM_LINELIST ||
        primitive_topology == _3DPRIM_LINESTRIP ||
        primitive_topology == _3DPRIM_LINELIST_ADJ ||
        primitive_topology == _3DPRIM_LINESTRIP_ADJ ||
        primitive_topology == _3DPRIM_LINELOOP ||
        primitive_topology == _3DPRIM_POINTLIST_BF ||
        primitive_topology == _3DPRIM_LINESTRIP_CONT ||
        primitive_topology == _3DPRIM_LINESTRIP_BF ||
        primitive_topology == _3DPRIM_LINESTRIP_CONT_BF) &&
       (vertex_count == 1 || vertex_count == 2)) {
      genx_batch_emit_pipe_control_write
         (batch, device->info, 0, WriteImmediateData,
          device->workaround_address, 0, 0);

      /* Reset counter because we just emitted a PC */
      batch->num_3d_primitives_emitted = 0;
   } else if (intel_needs_workaround(device->info, 16014538804)) {
      batch->num_3d_primitives_emitted++;
      /* WA 16014538804:
       *    After every 3 3D_Primitive command,
       *    atleast 1 pipe_control must be inserted.
       */
      if (batch->num_3d_primitives_emitted == 3) {
         anv_batch_emit(batch, GENX(PIPE_CONTROL), pc);
         batch->num_3d_primitives_emitted = 0;
      }
   }
#endif
}

void
genX(batch_emit_post_3dprimitive_was)(struct anv_batch *batch,
                                      const struct anv_device *device,
                                      uint32_t primitive_topology,
                                      uint32_t vertex_count)
{
   batch_post_draw_wa(batch, device, primitive_topology, vertex_count);
}

ALWAYS_INLINE static void
cmd_buffer_post_draw_wa(struct anv_cmd_buffer *cmd_buffer,
                        uint32_t vertex_count,
                        uint32_t access_type)
{
   batch_post_draw_wa(&cmd_buffer->batch, cmd_buffer->device,
                      cmd_buffer->state.gfx.dyn_state.vft.PrimitiveTopologyType,
                      vertex_count);

   update_dirty_vbs_for_gfx8_vb_flush(cmd_buffer, access_type);

   genX(emit_breakpoint)(&cmd_buffer->batch, cmd_buffer->device, false);
}

#if GFX_VER >= 11
#define _3DPRIMITIVE_DIRECT GENX(3DPRIMITIVE_EXTENDED)
#else
#define _3DPRIMITIVE_DIRECT GENX(3DPRIMITIVE)
#endif

void genX(CmdDraw)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    vertexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstVertex,
    uint32_t                                    firstInstance)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   const uint32_t count =
      vertexCount * instanceCount * gfx->instance_multiplier;
   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw", count);
   trace_intel_begin_draw(&cmd_buffer->trace);

   /* Select pipeline here to allow
    * cmd_buffer_emit_vertex_constants_and_flush() without flushing before
    * cmd_buffer_flush_gfx_state().
    */
   genX(flush_pipeline_select_3d)(cmd_buffer);

#if GFX_VER < 11
   cmd_buffer_emit_vertex_constants_and_flush(cmd_buffer,
                                              get_gfx_vs_prog_data(gfx),
                                              firstVertex, firstInstance, 0,
                                              false /* force_flush */);
#endif

   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   cmd_buffer_pre_draw_wa(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, _3DPRIMITIVE_DIRECT, prim) {
      prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
#if GFX_VERx10 >= 125
      prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
      prim.VertexAccessType         = SEQUENTIAL;
      prim.VertexCountPerInstance   = vertexCount;
      prim.StartVertexLocation      = firstVertex;
      prim.InstanceCount            = instanceCount *
                                      gfx->instance_multiplier;
      prim.StartInstanceLocation    = firstInstance;
      prim.BaseVertexLocation       = 0;
#if GFX_VER >= 11
      prim.ExtendedParametersPresent = true;
      prim.ExtendedParameter0       = firstVertex;
      prim.ExtendedParameter1       = firstInstance;
      prim.ExtendedParameter2       = 0;
#endif
   }

   cmd_buffer_post_draw_wa(cmd_buffer, vertexCount, SEQUENTIAL);

   trace_intel_end_draw(&cmd_buffer->trace, count,
                        gfx->vs_source_hash,
                        gfx->fs_source_hash);
}

void genX(CmdDrawMultiEXT)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawInfoEXT                   *pVertexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   uint32_t i = 0;
#if GFX_VER < 11
   vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
      cmd_buffer_emit_vertex_constants_and_flush(cmd_buffer,
                                                 get_gfx_vs_prog_data(gfx),
                                                 draw->firstVertex,
                                                 firstInstance, i, !i);

      const uint32_t count =
         draw->vertexCount * instanceCount * gfx->instance_multiplier;
      anv_measure_snapshot(cmd_buffer,
                           INTEL_SNAPSHOT_DRAW,
                           "draw multi", count);
      trace_intel_begin_draw_multi(&cmd_buffer->trace);

      cmd_buffer_pre_draw_wa(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
         prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
         prim.VertexAccessType         = SEQUENTIAL;
         prim.VertexCountPerInstance   = draw->vertexCount;
         prim.StartVertexLocation      = draw->firstVertex;
         prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
         prim.StartInstanceLocation    = firstInstance;
         prim.BaseVertexLocation       = 0;
      }

      cmd_buffer_post_draw_wa(cmd_buffer, drawCount == 0 ? 0 :
                              pVertexInfo[drawCount - 1].vertexCount,
                              SEQUENTIAL);

      trace_intel_end_draw_multi(&cmd_buffer->trace, count,
                                 gfx->vs_source_hash,
                                 gfx->fs_source_hash);
   }
#else
   vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
      const uint32_t count = draw->vertexCount * instanceCount;
      anv_measure_snapshot(cmd_buffer,
                           INTEL_SNAPSHOT_DRAW,
                           "draw multi", count);
      trace_intel_begin_draw_multi(&cmd_buffer->trace);

      cmd_buffer_pre_draw_wa(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, _3DPRIMITIVE_DIRECT, prim) {
#if GFX_VERx10 >= 125
         prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
         prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
         prim.VertexAccessType         = SEQUENTIAL;
         prim.VertexCountPerInstance   = draw->vertexCount;
         prim.StartVertexLocation      = draw->firstVertex;
         prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
         prim.StartInstanceLocation    = firstInstance;
         prim.BaseVertexLocation       = 0;
         prim.ExtendedParametersPresent = true;
         prim.ExtendedParameter0       = draw->firstVertex;
         prim.ExtendedParameter1       = firstInstance;
         prim.ExtendedParameter2       = i;
      }

      cmd_buffer_post_draw_wa(cmd_buffer, drawCount == 0 ? 0 :
                              pVertexInfo[drawCount - 1].vertexCount,
                              SEQUENTIAL);

      trace_intel_end_draw_multi(&cmd_buffer->trace, count,
                                 gfx->vs_source_hash,
                                 gfx->fs_source_hash);
   }
#endif
}

void genX(CmdDrawIndexed)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   const uint32_t count =
      indexCount * instanceCount * gfx->instance_multiplier;
   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw indexed",
                        count);
   trace_intel_begin_draw_indexed(&cmd_buffer->trace);

   /* Select pipeline here to allow
    * cmd_buffer_emit_vertex_constants_and_flush() without flushing before
    * cmd_buffer_flush_gfx_state().
    */
   genX(flush_pipeline_select_3d)(cmd_buffer);

#if GFX_VER < 11
   const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);
   cmd_buffer_emit_vertex_constants_and_flush(cmd_buffer, vs_prog_data,
                                              vertexOffset, firstInstance,
                                              0, false /* force_flush */);
#endif

   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   cmd_buffer_pre_draw_wa(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, _3DPRIMITIVE_DIRECT, prim) {
      prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
#if GFX_VERx10 >= 125
      prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
      prim.VertexAccessType         = RANDOM;
      prim.VertexCountPerInstance   = indexCount;
      prim.StartVertexLocation      = firstIndex;
      prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
      prim.StartInstanceLocation    = firstInstance;
      prim.BaseVertexLocation       = vertexOffset;
#if GFX_VER >= 11
      prim.ExtendedParametersPresent = true;
      prim.ExtendedParameter0       = vertexOffset;
      prim.ExtendedParameter1       = firstInstance;
      prim.ExtendedParameter2       = 0;
#endif
   }

   cmd_buffer_post_draw_wa(cmd_buffer, indexCount, RANDOM);

   trace_intel_end_draw_indexed(&cmd_buffer->trace, count,
                                gfx->vs_source_hash,
                                gfx->fs_source_hash);
}

void genX(CmdDrawMultiIndexedEXT)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawIndexedInfoEXT            *pIndexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride,
    const int32_t                              *pVertexOffset)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   uint32_t i = 0;
#if GFX_VER < 11
   const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);
   if (pVertexOffset) {
      if (vs_prog_data->uses_drawid) {
         bool emitted = true;
         if (vs_prog_data->uses_firstvertex ||
             vs_prog_data->uses_baseinstance) {
            emit_base_vertex_instance(cmd_buffer, *pVertexOffset, firstInstance);
            emitted = true;
         }
         vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
            if (vs_prog_data->uses_drawid) {
               emit_draw_index(cmd_buffer, i);
               emitted = true;
            }
            /* Emitting draw index or vertex index BOs may result in needing
             * additional VF cache flushes.
             */
            if (emitted)
               genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

            const uint32_t count =
               draw->indexCount * instanceCount * gfx->instance_multiplier;
            anv_measure_snapshot(cmd_buffer,
                                 INTEL_SNAPSHOT_DRAW,
                                 "draw indexed multi",
                                 count);
            trace_intel_begin_draw_indexed_multi(&cmd_buffer->trace);

            cmd_buffer_pre_draw_wa(cmd_buffer);

            anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
               prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
               prim.VertexAccessType         = RANDOM;
               prim.VertexCountPerInstance   = draw->indexCount;
               prim.StartVertexLocation      = draw->firstIndex;
               prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
               prim.StartInstanceLocation    = firstInstance;
               prim.BaseVertexLocation       = *pVertexOffset;
            }

            cmd_buffer_post_draw_wa(cmd_buffer, drawCount == 0 ? 0 :
                                    pIndexInfo[drawCount - 1].indexCount,
                                    RANDOM);

            trace_intel_end_draw_indexed_multi(&cmd_buffer->trace, count,
                                               gfx->vs_source_hash,
                                               gfx->fs_source_hash);
            emitted = false;
         }
      } else {
         if (vs_prog_data->uses_firstvertex ||
             vs_prog_data->uses_baseinstance) {
            emit_base_vertex_instance(cmd_buffer, *pVertexOffset, firstInstance);
            /* Emitting draw index or vertex index BOs may result in needing
             * additional VF cache flushes.
             */
            genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
         }
         vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
            const uint32_t count =
               draw->indexCount * instanceCount * gfx->instance_multiplier;
            anv_measure_snapshot(cmd_buffer,
                                 INTEL_SNAPSHOT_DRAW,
                                 "draw indexed multi",
                                 count);
            trace_intel_begin_draw_indexed_multi(&cmd_buffer->trace);

            cmd_buffer_pre_draw_wa(cmd_buffer);

            anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
               prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
               prim.VertexAccessType         = RANDOM;
               prim.VertexCountPerInstance   = draw->indexCount;
               prim.StartVertexLocation      = draw->firstIndex;
               prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
               prim.StartInstanceLocation    = firstInstance;
               prim.BaseVertexLocation       = *pVertexOffset;
            }

            cmd_buffer_post_draw_wa(cmd_buffer, drawCount == 0 ? 0 :
                                    pIndexInfo[drawCount - 1].indexCount,
                                    RANDOM);

            trace_intel_end_draw_indexed_multi(&cmd_buffer->trace, count,
                                               gfx->vs_source_hash,
                                               gfx->fs_source_hash);
         }
      }
   } else {
      vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
         cmd_buffer_emit_vertex_constants_and_flush(cmd_buffer, vs_prog_data,
                                                    draw->vertexOffset,
                                                    firstInstance, i, i != 0);

         const uint32_t count =
            draw->indexCount * instanceCount * gfx->instance_multiplier;
         anv_measure_snapshot(cmd_buffer,
                              INTEL_SNAPSHOT_DRAW,
                              "draw indexed multi",
                              count);
         trace_intel_begin_draw_indexed_multi(&cmd_buffer->trace);

         cmd_buffer_pre_draw_wa(cmd_buffer);

         anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE), prim) {
            prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
            prim.VertexAccessType         = RANDOM;
            prim.VertexCountPerInstance   = draw->indexCount;
            prim.StartVertexLocation      = draw->firstIndex;
            prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
            prim.StartInstanceLocation    = firstInstance;
            prim.BaseVertexLocation       = draw->vertexOffset;
         }

         cmd_buffer_post_draw_wa(cmd_buffer, drawCount == 0 ? 0 :
                                 pIndexInfo[drawCount - 1].indexCount,
                                 RANDOM);

         trace_intel_end_draw_indexed_multi(&cmd_buffer->trace, count,
                                            gfx->vs_source_hash,
                                            gfx->fs_source_hash);
      }
   }
#else
   vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
      const uint32_t count =
         draw->indexCount * instanceCount * gfx->instance_multiplier;
      anv_measure_snapshot(cmd_buffer,
                           INTEL_SNAPSHOT_DRAW,
                           "draw indexed multi",
                           count);
      trace_intel_begin_draw_indexed_multi(&cmd_buffer->trace);

      cmd_buffer_pre_draw_wa(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, GENX(3DPRIMITIVE_EXTENDED), prim) {
#if GFX_VERx10 >= 125
         prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
         prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
         prim.VertexAccessType         = RANDOM;
         prim.VertexCountPerInstance   = draw->indexCount;
         prim.StartVertexLocation      = draw->firstIndex;
         prim.InstanceCount            = instanceCount * gfx->instance_multiplier;
         prim.StartInstanceLocation    = firstInstance;
         prim.BaseVertexLocation       = pVertexOffset ? *pVertexOffset : draw->vertexOffset;
         prim.ExtendedParametersPresent = true;
         prim.ExtendedParameter0       = pVertexOffset ? *pVertexOffset : draw->vertexOffset;
         prim.ExtendedParameter1       = firstInstance;
         prim.ExtendedParameter2       = i;
      }

      cmd_buffer_post_draw_wa(cmd_buffer, drawCount == 0 ? 0 :
                              pIndexInfo[drawCount - 1].indexCount,
                              RANDOM);

      trace_intel_end_draw_indexed_multi(&cmd_buffer->trace, count,
                                         gfx->vs_source_hash,
                                         gfx->fs_source_hash);
   }
#endif
}

/* Auto-Draw / Indirect Registers */
#define GFX7_3DPRIM_END_OFFSET          0x2420
#define GFX7_3DPRIM_START_VERTEX        0x2430
#define GFX7_3DPRIM_VERTEX_COUNT        0x2434
#define GFX7_3DPRIM_INSTANCE_COUNT      0x2438
#define GFX7_3DPRIM_START_INSTANCE      0x243C
#define GFX7_3DPRIM_BASE_VERTEX         0x2440

/* On Gen11+, we have three custom "extended parameters" which we can use to
 * provide extra system-generated values to shaders.  Our assignment of these
 * is arbitrary; we choose to assign them as follows:
 *
 *    gl_BaseVertex = XP0
 *    gl_BaseInstance = XP1
 *    gl_DrawID = XP2
 *
 * For gl_BaseInstance, we never actually have to set up the value because we
 * can just program 3DSTATE_VF_SGVS_2 to load it implicitly.  We can also do
 * that for gl_BaseVertex but it does the wrong thing for indexed draws.
 */
#define GEN11_3DPRIM_XP0                0x2690
#define GEN11_3DPRIM_XP1                0x2694
#define GEN11_3DPRIM_XP2                0x2698
#define GEN11_3DPRIM_XP_BASE_VERTEX     GEN11_3DPRIM_XP0
#define GEN11_3DPRIM_XP_BASE_INSTANCE   GEN11_3DPRIM_XP1
#define GEN11_3DPRIM_XP_DRAW_ID         GEN11_3DPRIM_XP2

void genX(CmdDrawIndirectByteCountEXT)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    VkBuffer                                    counterBuffer,
    VkDeviceSize                                counterBufferOffset,
    uint32_t                                    counterOffset,
    uint32_t                                    vertexStride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, counter_buffer, counterBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   /* firstVertex is always zero for this draw function */
   const uint32_t firstVertex = 0;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw indirect byte count",
                        instanceCount * gfx->instance_multiplier);
   trace_intel_begin_draw_indirect_byte_count(&cmd_buffer->trace);

   /* Select pipeline here to allow
    * cmd_buffer_emit_vertex_constants_and_flush() without flushing before
    * emit_base_vertex_instance() & emit_draw_index().
    */
   genX(flush_pipeline_select_3d)(cmd_buffer);

#if GFX_VER < 11
   const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);
   if (vs_prog_data->uses_firstvertex ||
       vs_prog_data->uses_baseinstance)
      emit_base_vertex_instance(cmd_buffer, firstVertex, firstInstance);
   if (vs_prog_data->uses_drawid)
      emit_draw_index(cmd_buffer, 0);
#endif

   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);
   const uint32_t mocs = anv_mocs_for_address(cmd_buffer->device, &counter_buffer->address);
   mi_builder_set_mocs(&b, mocs);
   struct mi_value count =
      mi_mem32(anv_address_add(counter_buffer->address,
                                   counterBufferOffset));
   if (counterOffset)
      count = mi_isub(&b, count, mi_imm(counterOffset));
   count = mi_udiv32_imm(&b, count, vertexStride);
   mi_store(&b, mi_reg32(GFX7_3DPRIM_VERTEX_COUNT), count);

   mi_store(&b, mi_reg32(GFX7_3DPRIM_START_VERTEX), mi_imm(firstVertex));
   assert(((uint64_t)instanceCount * gfx->instance_multiplier <= UINT32_MAX));
   mi_store(&b, mi_reg32(GFX7_3DPRIM_INSTANCE_COUNT),
            mi_imm(instanceCount * gfx->instance_multiplier));
   mi_store(&b, mi_reg32(GFX7_3DPRIM_START_INSTANCE), mi_imm(firstInstance));
   mi_store(&b, mi_reg32(GFX7_3DPRIM_BASE_VERTEX), mi_imm(0));

#if GFX_VER >= 11
   mi_store(&b, mi_reg32(GEN11_3DPRIM_XP_BASE_VERTEX),
                mi_imm(firstVertex));
   /* GEN11_3DPRIM_XP_BASE_INSTANCE is implicit */
   mi_store(&b, mi_reg32(GEN11_3DPRIM_XP_DRAW_ID), mi_imm(0));
#endif

   cmd_buffer_pre_draw_wa(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, _3DPRIMITIVE_DIRECT, prim) {
#if GFX_VERx10 >= 125
      prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
      prim.IndirectParameterEnable  = true;
      prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
      prim.VertexAccessType         = SEQUENTIAL;
#if GFX_VER >= 11
      prim.ExtendedParametersPresent = true;
#endif
   }

   cmd_buffer_post_draw_wa(cmd_buffer, 1, SEQUENTIAL);

   trace_intel_end_draw_indirect_byte_count(&cmd_buffer->trace,
                                            instanceCount * gfx->instance_multiplier,
                                            gfx->vs_source_hash,
                                            gfx->fs_source_hash);
}

static void
load_indirect_parameters(struct anv_cmd_buffer *cmd_buffer,
                         struct anv_address addr,
                         bool indexed,
                         uint32_t draw_id)
{
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);
   const uint32_t mocs = anv_mocs_for_address(cmd_buffer->device, &addr);
   mi_builder_set_mocs(&b, mocs);

   mi_store(&b, mi_reg32(GFX7_3DPRIM_VERTEX_COUNT),
                mi_mem32(anv_address_add(addr, 0)));

   struct mi_value instance_count = mi_mem32(anv_address_add(addr, 4));
   if (gfx->instance_multiplier > 1) {
      instance_count = mi_imul_imm(&b, instance_count,
                                   gfx->instance_multiplier);
   }
   mi_store(&b, mi_reg32(GFX7_3DPRIM_INSTANCE_COUNT), instance_count);

   mi_store(&b, mi_reg32(GFX7_3DPRIM_START_VERTEX),
                mi_mem32(anv_address_add(addr, 8)));

   if (indexed) {
      mi_store(&b, mi_reg32(GFX7_3DPRIM_BASE_VERTEX),
                   mi_mem32(anv_address_add(addr, 12)));
      mi_store(&b, mi_reg32(GFX7_3DPRIM_START_INSTANCE),
                   mi_mem32(anv_address_add(addr, 16)));
#if GFX_VER >= 11
      mi_store(&b, mi_reg32(GEN11_3DPRIM_XP_BASE_VERTEX),
                   mi_mem32(anv_address_add(addr, 12)));
      /* GEN11_3DPRIM_XP_BASE_INSTANCE is implicit */
#endif
   } else {
      mi_store(&b, mi_reg32(GFX7_3DPRIM_START_INSTANCE),
                   mi_mem32(anv_address_add(addr, 12)));
      mi_store(&b, mi_reg32(GFX7_3DPRIM_BASE_VERTEX), mi_imm(0));
#if GFX_VER >= 11
      mi_store(&b, mi_reg32(GEN11_3DPRIM_XP_BASE_VERTEX),
                   mi_mem32(anv_address_add(addr, 8)));
      /* GEN11_3DPRIM_XP_BASE_INSTANCE is implicit */
#endif
   }

#if GFX_VER >= 11
   mi_store(&b, mi_reg32(GEN11_3DPRIM_XP_DRAW_ID),
                mi_imm(draw_id));
#endif
}

static inline bool
execute_indirect_draw_supported(const struct anv_cmd_buffer *cmd_buffer)
{
#if GFX_VERx10 >= 125
   const struct intel_device_info *devinfo = cmd_buffer->device->info;

   if (!devinfo->has_indirect_unroll)
      return false;


   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);
   const struct brw_task_prog_data *task_prog_data = get_gfx_task_prog_data(gfx);
   const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);
   const bool is_multiview = gfx->instance_multiplier > 1;

   const bool uses_draw_id =
      (vs_prog_data && vs_prog_data->uses_drawid) ||
      (mesh_prog_data && mesh_prog_data->uses_drawid) ||
      (task_prog_data && task_prog_data->uses_drawid);

   const bool uses_firstvertex =
      (vs_prog_data && vs_prog_data->uses_firstvertex);

   const bool uses_baseinstance =
      (vs_prog_data && vs_prog_data->uses_baseinstance);

   return !is_multiview &&
          !uses_draw_id &&
          !uses_firstvertex &&
          !uses_baseinstance;
#else
   return false;
#endif
}

static void
emit_indirect_draws(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_address indirect_data_addr,
                    uint32_t indirect_data_stride,
                    uint32_t draw_count,
                    bool indexed)
{
#if GFX_VER < 11
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);
#endif
   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   uint32_t offset = 0;
   for (uint32_t i = 0; i < draw_count; i++) {
      struct anv_address draw = anv_address_add(indirect_data_addr, offset);

#if GFX_VER < 11
      /* TODO: We need to stomp base vertex to 0 somehow */

      /* With sequential draws, we're dealing with the VkDrawIndirectCommand
       * structure data. We want to load VkDrawIndirectCommand::firstVertex at
       * offset 8 in the structure.
       *
       * With indexed draws, we're dealing with VkDrawIndexedIndirectCommand.
       * We want the VkDrawIndirectCommand::vertexOffset field at offset 12 in
       * the structure.
       */
      if (vs_prog_data->uses_firstvertex ||
          vs_prog_data->uses_baseinstance) {
         emit_base_vertex_instance_bo(cmd_buffer,
                                      anv_address_add(draw, indexed ? 12 : 8));
      }
      if (vs_prog_data->uses_drawid)
         emit_draw_index(cmd_buffer, i);
#endif

      /* Emitting draw index or vertex index BOs may result in needing
       * additional VF cache flushes.
       */
      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

      load_indirect_parameters(cmd_buffer, draw, indexed, i);

      cmd_buffer_pre_draw_wa(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, _3DPRIMITIVE_DIRECT, prim) {
#if GFX_VERx10 >= 125
         prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
         prim.IndirectParameterEnable  = true;
         prim.PredicateEnable          = cmd_buffer->state.conditional_render_enabled;
         prim.VertexAccessType         = indexed ? RANDOM : SEQUENTIAL;
#if GFX_VER >= 11
         prim.ExtendedParametersPresent = true;
#endif
      }

      cmd_buffer_post_draw_wa(cmd_buffer, 1, indexed ? RANDOM : SEQUENTIAL);

      offset += indirect_data_stride;
   }
}

static inline uint32_t xi_argument_format_for_vk_cmd(enum vk_cmd_type cmd)
{
#if GFX_VERx10 >= 125
   switch (cmd) {
      case VK_CMD_DRAW_INDIRECT:
      case VK_CMD_DRAW_INDIRECT_COUNT:
         return XI_DRAW;
      case VK_CMD_DRAW_INDEXED_INDIRECT:
      case VK_CMD_DRAW_INDEXED_INDIRECT_COUNT:
         return XI_DRAWINDEXED;
      case VK_CMD_DRAW_MESH_TASKS_INDIRECT_EXT:
      case VK_CMD_DRAW_MESH_TASKS_INDIRECT_COUNT_EXT:
         return XI_MESH_3D;
      default:
         UNREACHABLE("unhandled cmd type");
   }
#else
   UNREACHABLE("unsupported GFX VER");
#endif
}

static inline bool
cmd_buffer_set_indirect_stride(struct anv_cmd_buffer *cmd_buffer,
                               uint32_t stride, enum vk_cmd_type cmd)
{
   /* Should have been sanitized by the caller */
   assert(stride != 0);

   uint32_t data_stride = 0;

   switch (cmd) {
   case VK_CMD_DRAW_INDIRECT:
   case VK_CMD_DRAW_INDIRECT_COUNT:
      data_stride = sizeof(VkDrawIndirectCommand);
      break;
   case VK_CMD_DRAW_INDEXED_INDIRECT:
   case VK_CMD_DRAW_INDEXED_INDIRECT_COUNT:
      data_stride = sizeof(VkDrawIndexedIndirectCommand);
      break;
   case VK_CMD_DRAW_MESH_TASKS_INDIRECT_EXT:
   case VK_CMD_DRAW_MESH_TASKS_INDIRECT_COUNT_EXT:
      data_stride = sizeof(VkDrawMeshTasksIndirectCommandEXT);
      break;
   default:
      UNREACHABLE("unhandled cmd type");
   }

   enum u_tristate aligned = u_tristate_make(stride == data_stride);

#if GFX_VER >= 20
   /* The stride can change as long as it matches the default command stride
    * and STATE_BYTE_STRIDE::ByteStrideEnable=false, we can just do nothing.
    *
    * Otheriwse STATE_BYTE_STRIDE::ByteStrideEnable=true, any stride change
    * should be signaled.
    */
   struct anv_cmd_graphics_state *gfx_state = &cmd_buffer->state.gfx;
   if (gfx_state->indirect_data_stride_aligned != aligned) {
      gfx_state->indirect_data_stride = stride;
      gfx_state->indirect_data_stride_aligned = aligned;
      gfx_state->dirty |= ANV_CMD_DIRTY_INDIRECT_DATA_STRIDE;
   } else if (gfx_state->indirect_data_stride_aligned == U_TRISTATE_NO &&
              gfx_state->indirect_data_stride != stride) {
      gfx_state->indirect_data_stride = stride;
      gfx_state->indirect_data_stride_aligned = aligned;
      gfx_state->dirty |= ANV_CMD_DIRTY_INDIRECT_DATA_STRIDE;
   }
#endif

   return aligned;
}

static void
genX(cmd_buffer_emit_execute_indirect_draws)(struct anv_cmd_buffer *cmd_buffer,
                                             struct anv_address indirect_data_addr,
                                             uint32_t indirect_data_stride,
                                             struct anv_address count_addr,
                                             uint32_t max_draw_count,
                                             enum vk_cmd_type cmd)
{
#if GFX_VERx10 >= 125
   bool aligned_stride =
      cmd_buffer_set_indirect_stride(cmd_buffer, indirect_data_stride, cmd);

   genX(cmd_buffer_flush_gfx_state)(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   uint32_t offset = 0;
   for (uint32_t i = 0; i < max_draw_count; i++) {
      struct anv_address draw = anv_address_add(indirect_data_addr, offset);

      cmd_buffer_pre_draw_wa(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, GENX(EXECUTE_INDIRECT_DRAW), ind) {
         ind.ArgumentFormat             = xi_argument_format_for_vk_cmd(cmd);
         ind.TBIMREnabled               = cmd_buffer->state.gfx.dyn_state.use_tbimr;
         ind.PredicateEnable            =
            cmd_buffer->state.conditional_render_enabled;
         ind.MaxCount                   = aligned_stride ? max_draw_count : 1;
         ind.ArgumentBufferStartAddress = draw;
         ind.CountBufferAddress         = count_addr;
         ind.CountBufferIndirectEnable  = !anv_address_is_null(count_addr);
         ind.MOCS                       =
            anv_mocs(cmd_buffer->device, draw.bo, 0);

      }

      cmd_buffer_post_draw_wa(cmd_buffer, 1,
                              0 /* Doesn't matter for GFX_VER > 9 */);

      /* If all the indirect structures are aligned, then we can let the HW
       * do the unrolling and we only need one instruction. Otherwise we
       * need to emit one instruction per draw, but we're still avoiding
       * the register loads with MI commands.
       */
      if (aligned_stride || GFX_VER >= 20)
         break;

      offset += indirect_data_stride;
   }
#endif // GFX_VERx10 >= 125
}
void genX(CmdDrawIndirect)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw indirect",
                        drawCount);
   trace_intel_begin_draw_indirect(&cmd_buffer->trace);

   struct anv_address indirect_data_addr =
      anv_address_add(buffer->address, offset);

   stride = MAX2(stride, sizeof(VkDrawIndirectCommand));

   if (execute_indirect_draw_supported(cmd_buffer)) {
      genX(cmd_buffer_emit_execute_indirect_draws)(
         cmd_buffer,
         indirect_data_addr,
         stride,
         ANV_NULL_ADDRESS /* count_addr */,
         drawCount,
         VK_CMD_DRAW_INDIRECT);
   } else if (anv_use_generated_draws(cmd_buffer, drawCount)) {
      genX(cmd_buffer_emit_indirect_generated_draws)(
         cmd_buffer,
         indirect_data_addr,
         stride,
         ANV_NULL_ADDRESS /* count_addr */,
         drawCount,
         false /* indexed */);
   } else {
      emit_indirect_draws(cmd_buffer,
                          indirect_data_addr,
                          stride, drawCount, false /* indexed */);
   }

   trace_intel_end_draw_indirect(&cmd_buffer->trace, drawCount,
                                 gfx->vs_source_hash,
                                 gfx->fs_source_hash);
}

void genX(CmdDrawIndexedIndirect)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw indexed indirect",
                        drawCount);
   trace_intel_begin_draw_indexed_indirect(&cmd_buffer->trace);

   struct anv_address indirect_data_addr =
      anv_address_add(buffer->address, offset);

   stride = MAX2(stride, sizeof(VkDrawIndexedIndirectCommand));

   if (execute_indirect_draw_supported(cmd_buffer)) {
      genX(cmd_buffer_emit_execute_indirect_draws)(
         cmd_buffer,
         indirect_data_addr,
         stride,
         ANV_NULL_ADDRESS /* count_addr */,
         drawCount,
         VK_CMD_DRAW_INDEXED_INDIRECT);
   } else if (anv_use_generated_draws(cmd_buffer, drawCount)) {
      genX(cmd_buffer_emit_indirect_generated_draws)(
         cmd_buffer,
         indirect_data_addr,
         stride,
         ANV_NULL_ADDRESS /* count_addr */,
         drawCount,
         true /* indexed */);
   } else {
      emit_indirect_draws(cmd_buffer,
                          indirect_data_addr,
                          stride, drawCount, true /* indexed */);
   }

   trace_intel_end_draw_indexed_indirect(&cmd_buffer->trace, drawCount,
                                         gfx->vs_source_hash,
                                         gfx->fs_source_hash);
}

#define MI_PREDICATE_SRC0    0x2400
#define MI_PREDICATE_SRC1    0x2408
#define MI_PREDICATE_RESULT  0x2418

static struct mi_value
prepare_for_draw_count_predicate(struct anv_cmd_buffer *cmd_buffer,
                                 struct mi_builder *b,
                                 struct anv_address count_address)
{
   struct mi_value ret = mi_imm(0);

   if (cmd_buffer->state.conditional_render_enabled) {
      ret = mi_new_gpr(b);
      mi_store(b, mi_value_ref(b, ret), mi_mem32(count_address));
   } else {
      /* Upload the current draw count from the draw parameters buffer to
       * MI_PREDICATE_SRC0.
       */
      mi_store(b, mi_reg64(MI_PREDICATE_SRC0), mi_mem32(count_address));
      mi_store(b, mi_reg32(MI_PREDICATE_SRC1 + 4), mi_imm(0));
   }

   return ret;
}

static void
emit_draw_count_predicate(struct anv_cmd_buffer *cmd_buffer,
                          struct mi_builder *b,
                          uint32_t draw_index)
{
   /* Upload the index of the current primitive to MI_PREDICATE_SRC1. */
   mi_store(b, mi_reg32(MI_PREDICATE_SRC1), mi_imm(draw_index));

   if (draw_index == 0) {
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_PREDICATE), mip) {
         mip.LoadOperation    = LOAD_LOADINV;
         mip.CombineOperation = COMBINE_SET;
         mip.CompareOperation = COMPARE_SRCS_EQUAL;
      }
   } else {
      /* While draw_index < draw_count the predicate's result will be
       *  (draw_index == draw_count) ^ TRUE = TRUE
       * When draw_index == draw_count the result is
       *  (TRUE) ^ TRUE = FALSE
       * After this all results will be:
       *  (FALSE) ^ FALSE = FALSE
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_PREDICATE), mip) {
         mip.LoadOperation    = LOAD_LOAD;
         mip.CombineOperation = COMBINE_XOR;
         mip.CompareOperation = COMPARE_SRCS_EQUAL;
      }
   }
}

static void
emit_draw_count_predicate_with_conditional_render(
                          struct anv_cmd_buffer *cmd_buffer,
                          struct mi_builder *b,
                          uint32_t draw_index,
                          struct mi_value max)
{
   struct mi_value pred = mi_ult(b, mi_imm(draw_index), max);
   pred = mi_iand(b, pred, mi_reg64(ANV_PREDICATE_RESULT_REG));

   mi_store(b, mi_reg32(MI_PREDICATE_RESULT), pred);
}

static void
emit_draw_count_predicate_cond(struct anv_cmd_buffer *cmd_buffer,
                               struct mi_builder *b,
                               uint32_t draw_index,
                               struct mi_value max)
{
   if (cmd_buffer->state.conditional_render_enabled) {
      emit_draw_count_predicate_with_conditional_render(
            cmd_buffer, b, draw_index, mi_value_ref(b, max));
   } else {
      emit_draw_count_predicate(cmd_buffer, b, draw_index);
   }
}

static void
emit_indirect_count_draws(struct anv_cmd_buffer *cmd_buffer,
                          struct anv_address indirect_data_addr,
                          uint64_t indirect_data_stride,
                          struct anv_address draw_count_addr,
                          uint32_t max_draw_count,
                          bool indexed)
{
#if GFX_VER < 11
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct brw_vs_prog_data *vs_prog_data = get_gfx_vs_prog_data(gfx);
#endif

   cmd_buffer_flush_gfx_state(cmd_buffer);

   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);
   const uint32_t mocs = anv_mocs_for_address(cmd_buffer->device, &draw_count_addr);
   mi_builder_set_mocs(&b, mocs);
   struct mi_value max =
      prepare_for_draw_count_predicate(cmd_buffer, &b, draw_count_addr);

   for (uint32_t i = 0; i < max_draw_count; i++) {
      struct anv_address draw =
         anv_address_add(indirect_data_addr, i * indirect_data_stride);

      emit_draw_count_predicate_cond(cmd_buffer, &b, i, max);

#if GFX_VER < 11
      if (vs_prog_data->uses_firstvertex ||
          vs_prog_data->uses_baseinstance) {
         emit_base_vertex_instance_bo(cmd_buffer,
                                      anv_address_add(draw, indexed ? 12 : 8));
      }
      if (vs_prog_data->uses_drawid)
         emit_draw_index(cmd_buffer, i);

      /* Emitting draw index or vertex index BOs may result in needing
       * additional VF cache flushes.
       */
      genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);
#endif

      load_indirect_parameters(cmd_buffer, draw, indexed, i);

      cmd_buffer_pre_draw_wa(cmd_buffer);

      anv_batch_emit(&cmd_buffer->batch, _3DPRIMITIVE_DIRECT, prim) {
#if GFX_VERx10 >= 125
         prim.TBIMREnable = cmd_buffer->state.gfx.dyn_state.use_tbimr;
#endif
         prim.IndirectParameterEnable  = true;
         prim.PredicateEnable          = true;
         prim.VertexAccessType         = indexed ? RANDOM : SEQUENTIAL;
#if GFX_VER >= 11
         prim.ExtendedParametersPresent = true;
#endif
      }

      cmd_buffer_post_draw_wa(cmd_buffer, 1, SEQUENTIAL);
   }

   mi_value_unref(&b, max);
}

void genX(CmdDrawIndirectCount)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    _countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   ANV_FROM_HANDLE(anv_buffer, count_buffer, _countBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw indirect count",
                        0);
   trace_intel_begin_draw_indirect_count(&cmd_buffer->trace);

   struct anv_address indirect_data_address =
      anv_address_add(buffer->address, offset);
   struct anv_address count_address =
      anv_address_add(count_buffer->address, countBufferOffset);
   stride = MAX2(stride, sizeof(VkDrawIndirectCommand));

   if (execute_indirect_draw_supported(cmd_buffer)) {
      genX(cmd_buffer_emit_execute_indirect_draws)(
         cmd_buffer,
         indirect_data_address,
         stride,
         count_address,
         maxDrawCount,
         VK_CMD_DRAW_INDIRECT_COUNT);
   } else if (anv_use_generated_draws(cmd_buffer, maxDrawCount)) {
      genX(cmd_buffer_emit_indirect_generated_draws)(
         cmd_buffer,
         indirect_data_address,
         stride,
         count_address,
         maxDrawCount,
         false /* indexed */);
   } else {
      emit_indirect_count_draws(cmd_buffer,
                                indirect_data_address,
                                stride,
                                count_address,
                                maxDrawCount,
                                false /* indexed */);
   }

   trace_intel_end_draw_indirect_count(&cmd_buffer->trace,
                                       anv_address_utrace(count_address),
                                       gfx->vs_source_hash,
                                       gfx->fs_source_hash);
}

void genX(CmdDrawIndexedIndirectCount)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    _countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   ANV_FROM_HANDLE(anv_buffer, count_buffer, _countBuffer);
   const struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw indexed indirect count",
                        0);
   trace_intel_begin_draw_indexed_indirect_count(&cmd_buffer->trace);

   struct anv_address indirect_data_address =
      anv_address_add(buffer->address, offset);
   struct anv_address count_address =
      anv_address_add(count_buffer->address, countBufferOffset);
   stride = MAX2(stride, sizeof(VkDrawIndexedIndirectCommand));

   if (execute_indirect_draw_supported(cmd_buffer)) {
      genX(cmd_buffer_emit_execute_indirect_draws)(
         cmd_buffer,
         indirect_data_address,
         stride,
         count_address,
         maxDrawCount,
         VK_CMD_DRAW_INDEXED_INDIRECT_COUNT);
   } else if (anv_use_generated_draws(cmd_buffer, maxDrawCount)) {
      genX(cmd_buffer_emit_indirect_generated_draws)(
         cmd_buffer,
         indirect_data_address,
         stride,
         count_address,
         maxDrawCount,
         true /* indexed */);
   } else {
      emit_indirect_count_draws(cmd_buffer,
                                indirect_data_address,
                                stride,
                                count_address,
                                maxDrawCount,
                                true /* indexed */);
   }

   trace_intel_end_draw_indexed_indirect_count(&cmd_buffer->trace,
                                               anv_address_utrace(count_address),
                                               gfx->vs_source_hash,
                                               gfx->fs_source_hash);
}

void genX(CmdBeginTransformFeedbackEXT)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(firstCounterBuffer < MAX_XFB_BUFFERS);
   assert(counterBufferCount <= MAX_XFB_BUFFERS);
   assert(firstCounterBuffer + counterBufferCount <= MAX_XFB_BUFFERS);

   trace_intel_begin_xfb(&cmd_buffer->trace);

   /* From the SKL PRM Vol. 2c, SO_WRITE_OFFSET:
    *
    *    "Ssoftware must ensure that no HW stream output operations can be in
    *    process or otherwise pending at the point that the MI_LOAD/STORE
    *    commands are processed. This will likely require a pipeline flush."
    */
   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_CS_STALL_BIT,
                             "begin transform feedback");
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);

   for (uint32_t idx = 0; idx < MAX_XFB_BUFFERS; idx++) {
      /* If we have a counter buffer, this is a resume so we need to load the
       * value into the streamout offset register.  Otherwise, this is a begin
       * and we need to reset it to zero.
       */
      if (pCounterBuffers &&
          idx >= firstCounterBuffer &&
          idx - firstCounterBuffer < counterBufferCount &&
          pCounterBuffers[idx - firstCounterBuffer] != VK_NULL_HANDLE) {
         uint32_t cb_idx = idx - firstCounterBuffer;
         ANV_FROM_HANDLE(anv_buffer, counter_buffer, pCounterBuffers[cb_idx]);
         uint64_t offset = pCounterBufferOffsets ?
                           pCounterBufferOffsets[cb_idx] : 0;
         mi_store(&b, mi_reg32(GENX(SO_WRITE_OFFSET0_num) + idx * 4),
                  mi_mem32(anv_address_add(counter_buffer->address, offset)));
      } else {
         mi_store(&b, mi_reg32(GENX(SO_WRITE_OFFSET0_num) + idx * 4),
                  mi_imm(0));
      }
   }

   cmd_buffer->state.xfb_enabled = true;
   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_XFB_ENABLE;
}

void genX(CmdEndTransformFeedbackEXT)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(firstCounterBuffer < MAX_XFB_BUFFERS);
   assert(counterBufferCount <= MAX_XFB_BUFFERS);
   assert(firstCounterBuffer + counterBufferCount <= MAX_XFB_BUFFERS);

   /* From the SKL PRM Vol. 2c, SO_WRITE_OFFSET:
    *
    *    "Ssoftware must ensure that no HW stream output operations can be in
    *    process or otherwise pending at the point that the MI_LOAD/STORE
    *    commands are processed. This will likely require a pipeline flush."
    */
   anv_add_pending_pipe_bits(cmd_buffer,
                             ANV_PIPE_CS_STALL_BIT,
                             "end transform feedback");
   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   for (uint32_t cb_idx = 0; cb_idx < counterBufferCount; cb_idx++) {
      unsigned idx = firstCounterBuffer + cb_idx;

      /* If we have a counter buffer, this is a resume so we need to load the
       * value into the streamout offset register.  Otherwise, this is a begin
       * and we need to reset it to zero.
       */
      if (pCounterBuffers &&
          cb_idx < counterBufferCount &&
          pCounterBuffers[cb_idx] != VK_NULL_HANDLE) {
         ANV_FROM_HANDLE(anv_buffer, counter_buffer, pCounterBuffers[cb_idx]);
         uint64_t offset = pCounterBufferOffsets ?
                           pCounterBufferOffsets[cb_idx] : 0;

         anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
            srm.MemoryAddress    = anv_address_add(counter_buffer->address,
                                                   offset);
            srm.RegisterAddress  = GENX(SO_WRITE_OFFSET0_num) + idx * 4;
         }
      }
   }

   trace_intel_end_xfb(&cmd_buffer->trace);

   cmd_buffer->state.xfb_enabled = false;
   cmd_buffer->state.gfx.dirty |= ANV_CMD_DIRTY_XFB_ENABLE;
}

#if GFX_VERx10 >= 125

void
genX(CmdDrawMeshTasksEXT)(
      VkCommandBuffer commandBuffer,
      uint32_t x,
      uint32_t y,
      uint32_t z)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw mesh", x * y * z);

   trace_intel_begin_draw_mesh(&cmd_buffer->trace);

   /* TODO(mesh): Check if this is not emitting more packets than we need. */
   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_buffer->state.conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   anv_batch_emit(&cmd_buffer->batch, GENX(3DMESH_3D), m) {
      m.PredicateEnable = cmd_buffer->state.conditional_render_enabled;
      m.ThreadGroupCountX = x;
      m.ThreadGroupCountY = y;
      m.ThreadGroupCountZ = z;
   }

   trace_intel_end_draw_mesh(&cmd_buffer->trace, x, y, z);
}

#define GFX125_3DMESH_TG_COUNT 0x26F0
#define GFX10_3DPRIM_XP(n) (0x2690 + (n) * 4) /* n = { 0, 1, 2 } */

static void
mesh_load_indirect_parameters_3dmesh_3d(struct anv_cmd_buffer *cmd_buffer,
                                        struct mi_builder *b,
                                        struct anv_address addr,
                                        bool emit_xp0,
                                        uint32_t xp0)
{
   const size_t groupCountXOff = offsetof(VkDrawMeshTasksIndirectCommandEXT, groupCountX);
   const size_t groupCountYOff = offsetof(VkDrawMeshTasksIndirectCommandEXT, groupCountY);
   const size_t groupCountZOff = offsetof(VkDrawMeshTasksIndirectCommandEXT, groupCountZ);

   mi_store(b, mi_reg32(GFX125_3DMESH_TG_COUNT),
               mi_mem32(anv_address_add(addr, groupCountXOff)));

   mi_store(b, mi_reg32(GFX10_3DPRIM_XP(1)),
               mi_mem32(anv_address_add(addr, groupCountYOff)));

   mi_store(b, mi_reg32(GFX10_3DPRIM_XP(2)),
               mi_mem32(anv_address_add(addr, groupCountZOff)));

   if (emit_xp0)
      mi_store(b, mi_reg32(GFX10_3DPRIM_XP(0)), mi_imm(xp0));
}

static void
emit_indirect_3dmesh_3d(struct anv_batch *batch,
                        bool predicate_enable,
                        bool uses_drawid)
{
   uint32_t len = GENX(3DMESH_3D_length) + uses_drawid;
   uint32_t *dw = anv_batch_emitn(batch, len, GENX(3DMESH_3D),
                   .PredicateEnable           = predicate_enable,
                   .IndirectParameterEnable   = true,
                   .ExtendedParameter0Present = uses_drawid);
   if (uses_drawid)
      dw[len - 1] = 0;
}

void
genX(CmdDrawMeshTasksIndirectEXT)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    uint32_t                                    drawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct brw_task_prog_data *task_prog_data = get_gfx_task_prog_data(gfx);
   const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);
   struct anv_cmd_state *cmd_state = &cmd_buffer->state;

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw mesh indirect", drawCount);

   trace_intel_begin_draw_mesh_indirect(&cmd_buffer->trace);

   if (execute_indirect_draw_supported(cmd_buffer)) {
      genX(cmd_buffer_emit_execute_indirect_draws)(
         cmd_buffer,
         anv_address_add(buffer->address, offset),
         MAX2(stride, sizeof(VkDrawMeshTasksIndirectCommandEXT)),
         ANV_NULL_ADDRESS /* count_addr */,
         drawCount,
         VK_CMD_DRAW_MESH_TASKS_INDIRECT_EXT);

      trace_intel_end_draw_mesh_indirect(&cmd_buffer->trace, drawCount);
      return;
   }

   cmd_buffer_flush_gfx_state(cmd_buffer);

   if (cmd_state->conditional_render_enabled)
      genX(cmd_emit_conditional_render_predicate)(cmd_buffer);

   bool uses_drawid = (task_prog_data && task_prog_data->uses_drawid) ||
                       mesh_prog_data->uses_drawid;
   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);

   for (uint32_t i = 0; i < drawCount; i++) {
      struct anv_address draw = anv_address_add(buffer->address, offset);

      mesh_load_indirect_parameters_3dmesh_3d(cmd_buffer, &b, draw, uses_drawid, i);

      emit_indirect_3dmesh_3d(&cmd_buffer->batch,
            cmd_state->conditional_render_enabled, uses_drawid);

      offset += stride;
   }

   trace_intel_end_draw_mesh_indirect(&cmd_buffer->trace, drawCount);
}

void
genX(CmdDrawMeshTasksIndirectCountEXT)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    _countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   ANV_FROM_HANDLE(anv_buffer, count_buffer, _countBuffer);
   struct anv_cmd_graphics_state *gfx = &cmd_buffer->state.gfx;
   const struct brw_task_prog_data *task_prog_data = get_gfx_task_prog_data(gfx);
   const struct brw_mesh_prog_data *mesh_prog_data = get_gfx_mesh_prog_data(gfx);

   if (anv_batch_has_error(&cmd_buffer->batch))
      return;

   anv_measure_snapshot(cmd_buffer,
                        INTEL_SNAPSHOT_DRAW,
                        "draw mesh indirect count", 0);

   trace_intel_begin_draw_mesh_indirect_count(&cmd_buffer->trace);

   struct anv_address count_addr =
      anv_address_add(count_buffer->address, countBufferOffset);


   if (execute_indirect_draw_supported(cmd_buffer)) {
      genX(cmd_buffer_emit_execute_indirect_draws)(
         cmd_buffer,
         anv_address_add(buffer->address, offset),
         MAX2(stride, sizeof(VkDrawMeshTasksIndirectCommandEXT)),
         count_addr /* count_addr */,
         maxDrawCount,
         VK_CMD_DRAW_MESH_TASKS_INDIRECT_COUNT_EXT);

      trace_intel_end_draw_mesh_indirect(&cmd_buffer->trace, maxDrawCount);
      return;
   }

   cmd_buffer_flush_gfx_state(cmd_buffer);

   bool uses_drawid = (task_prog_data && task_prog_data->uses_drawid) ||
                       mesh_prog_data->uses_drawid;

   struct mi_builder b;
   mi_builder_init(&b, cmd_buffer->device->info, &cmd_buffer->batch);
   const uint32_t mocs = anv_mocs_for_address(cmd_buffer->device, &count_buffer->address);
   mi_builder_set_mocs(&b, mocs);

   struct mi_value max =
         prepare_for_draw_count_predicate(
            cmd_buffer, &b, count_addr);

   for (uint32_t i = 0; i < maxDrawCount; i++) {
      struct anv_address draw = anv_address_add(buffer->address, offset);

      emit_draw_count_predicate_cond(cmd_buffer, &b, i, max);

      mesh_load_indirect_parameters_3dmesh_3d(cmd_buffer, &b, draw, uses_drawid, i);

      emit_indirect_3dmesh_3d(&cmd_buffer->batch, true, uses_drawid);

      offset += stride;
   }

   trace_intel_end_draw_mesh_indirect_count(&cmd_buffer->trace,
                                            anv_address_utrace(count_addr));
}

#endif /* GFX_VERx10 >= 125 */
