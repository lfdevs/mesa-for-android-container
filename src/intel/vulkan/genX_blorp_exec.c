/*
 * Copyright © 2016 Intel Corporation
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

#include "anv_private.h"

/* These are defined in anv_private.h and blorp_genX_exec.h */
#undef __gen_address_type
#undef __gen_user_data
#undef __gen_combine_address

#include "common/gen_l3_config.h"
#include "common/gen_sample_positions.h"
#include "blorp/blorp_genX_exec.h"

static void *
blorp_emit_dwords(struct blorp_batch *batch, unsigned n)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;
   return anv_batch_emit_dwords(&cmd_buffer->batch, n);
}

static uint64_t
blorp_emit_reloc(struct blorp_batch *batch,
                 void *location, struct blorp_address address, uint32_t delta)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;
   assert(cmd_buffer->batch.start <= location &&
          location < cmd_buffer->batch.end);
   return anv_batch_emit_reloc(&cmd_buffer->batch, location,
                               address.buffer, address.offset + delta);
}

static void
blorp_surface_reloc(struct blorp_batch *batch, uint32_t ss_offset,
                    struct blorp_address address, uint32_t delta)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;
   uint64_t address_u64 = 0;
   VkResult result =
      anv_reloc_list_add(&cmd_buffer->surface_relocs, &cmd_buffer->pool->alloc,
                         ss_offset, address.buffer, address.offset + delta,
                         &address_u64);
   if (result != VK_SUCCESS)
      anv_batch_set_error(&cmd_buffer->batch, result);

   void *dest = anv_block_pool_map(
      &cmd_buffer->device->surface_state_pool.block_pool, ss_offset, 8);
   write_reloc(cmd_buffer->device, dest, address_u64, false);
}

static uint64_t
blorp_get_surface_address(struct blorp_batch *blorp_batch,
                          struct blorp_address address)
{
   /* We'll let blorp_surface_reloc write the address. */
   return 0ull;
}

#if GEN_GEN >= 7 && GEN_GEN < 10
static struct blorp_address
blorp_get_surface_base_address(struct blorp_batch *batch)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;
   return (struct blorp_address) {
      .buffer = cmd_buffer->device->surface_state_pool.block_pool.bo,
      .offset = 0,
   };
}
#endif

static void *
blorp_alloc_dynamic_state(struct blorp_batch *batch,
                          uint32_t size,
                          uint32_t alignment,
                          uint32_t *offset)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;

   struct anv_state state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, size, alignment);

   *offset = state.offset;
   return state.map;
}

static void
blorp_alloc_binding_table(struct blorp_batch *batch, unsigned num_entries,
                          unsigned state_size, unsigned state_alignment,
                          uint32_t *bt_offset,
                          uint32_t *surface_offsets, void **surface_maps)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;

   uint32_t state_offset;
   struct anv_state bt_state;

   VkResult result =
      anv_cmd_buffer_alloc_blorp_binding_table(cmd_buffer, num_entries,
                                               &state_offset, &bt_state);
   if (result != VK_SUCCESS)
      return;

   uint32_t *bt_map = bt_state.map;
   *bt_offset = bt_state.offset;

   for (unsigned i = 0; i < num_entries; i++) {
      struct anv_state surface_state =
         anv_cmd_buffer_alloc_surface_state(cmd_buffer);
      bt_map[i] = surface_state.offset + state_offset;
      surface_offsets[i] = surface_state.offset;
      surface_maps[i] = surface_state.map;
   }
}

static void *
blorp_alloc_vertex_buffer(struct blorp_batch *batch, uint32_t size,
                          struct blorp_address *addr)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;
   struct anv_state vb_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, size, 64);

   *addr = (struct blorp_address) {
      .buffer = cmd_buffer->device->dynamic_state_pool.block_pool.bo,
      .offset = vb_state.offset,
      .mocs = isl_mocs(&cmd_buffer->device->isl_dev,
                       ISL_SURF_USAGE_VERTEX_BUFFER_BIT),
   };

   return vb_state.map;
}

static void
blorp_vf_invalidate_for_vb_48b_transitions(struct blorp_batch *batch,
                                           const struct blorp_address *addrs,
                                           uint32_t *sizes,
                                           unsigned num_vbs)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;

   for (unsigned i = 0; i < num_vbs; i++) {
      struct anv_address anv_addr = {
         .bo = addrs[i].buffer,
         .offset = addrs[i].offset,
      };
      genX(cmd_buffer_set_binding_for_gen8_vb_flush)(cmd_buffer,
                                                     i, anv_addr, sizes[i]);
   }

   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   /* Technically, we should call this *after* 3DPRIMITIVE but it doesn't
    * really matter for blorp because we never call apply_pipe_flushes after
    * this point.
    */
   genX(cmd_buffer_update_dirty_vbs_for_gen8_vb_flush)(cmd_buffer, SEQUENTIAL,
                                                       (1 << num_vbs) - 1);
}

UNUSED static struct blorp_address
blorp_get_workaround_address(struct blorp_batch *batch)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;

   return (struct blorp_address) {
      .buffer = cmd_buffer->device->workaround_address.bo,
      .offset = cmd_buffer->device->workaround_address.offset,
   };
}

static void
blorp_flush_range(struct blorp_batch *batch, void *start, size_t size)
{
   /* We don't need to flush states anymore, since everything will be snooped.
    */
}

static const struct gen_l3_config *
blorp_get_l3_config(struct blorp_batch *batch)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;
   return cmd_buffer->state.current_l3_config;
}

void
genX(blorp_exec)(struct blorp_batch *batch,
                 const struct blorp_params *params)
{
   struct anv_cmd_buffer *cmd_buffer = batch->driver_batch;

   if (!cmd_buffer->state.current_l3_config) {
      const struct gen_l3_config *cfg =
         gen_get_default_l3_config(&cmd_buffer->device->info);
      genX(cmd_buffer_config_l3)(cmd_buffer, cfg);
   }

   const unsigned scale = params->fast_clear_op ? UINT_MAX : 1;
   genX(cmd_buffer_emit_hashing_mode)(cmd_buffer, params->x1 - params->x0,
                                      params->y1 - params->y0, scale);

#if GEN_GEN >= 11
   /* The PIPE_CONTROL command description says:
    *
    *    "Whenever a Binding Table Index (BTI) used by a Render Taget Message
    *     points to a different RENDER_SURFACE_STATE, SW must issue a Render
    *     Target Cache Flush by enabling this bit. When render target flush
    *     is set due to new association of BTI, PS Scoreboard Stall bit must
    *     be set in this packet."
    */
   cmd_buffer->state.pending_pipe_bits |=
      ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT |
      ANV_PIPE_STALL_AT_SCOREBOARD_BIT;
#endif

#if GEN_GEN == 7
   /* The MI_LOAD/STORE_REGISTER_MEM commands which BLORP uses to implement
    * indirect fast-clear colors can cause GPU hangs if we don't stall first.
    * See genX(cmd_buffer_mi_memcpy) for more details.
    */
   if (params->src.clear_color_addr.buffer ||
       params->dst.clear_color_addr.buffer)
      cmd_buffer->state.pending_pipe_bits |= ANV_PIPE_CS_STALL_BIT;
#endif

   genX(cmd_buffer_apply_pipe_flushes)(cmd_buffer);

   genX(flush_pipeline_select_3d)(cmd_buffer);

   genX(cmd_buffer_emit_gen7_depth_flush)(cmd_buffer);

   /* BLORP doesn't do anything fancy with depth such as discards, so we want
    * the PMA fix off.  Also, off is always the safe option.
    */
   genX(cmd_buffer_enable_pma_fix)(cmd_buffer, false);

   blorp_exec(batch, params);

#if GEN_GEN >= 11
   /* The PIPE_CONTROL command description says:
    *
    *    "Whenever a Binding Table Index (BTI) used by a Render Taget Message
    *     points to a different RENDER_SURFACE_STATE, SW must issue a Render
    *     Target Cache Flush by enabling this bit. When render target flush
    *     is set due to new association of BTI, PS Scoreboard Stall bit must
    *     be set in this packet."
    */
   cmd_buffer->state.pending_pipe_bits |=
      ANV_PIPE_RENDER_TARGET_CACHE_FLUSH_BIT |
      ANV_PIPE_STALL_AT_SCOREBOARD_BIT;
#endif

   cmd_buffer->state.gfx.vb_dirty = ~0;
   cmd_buffer->state.gfx.dirty = ~0;
   cmd_buffer->state.push_constants_dirty = ~0;
}
