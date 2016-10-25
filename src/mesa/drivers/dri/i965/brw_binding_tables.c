/*
 * Copyright © 2013 Intel Corporation
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

/**
 * \file brw_binding_tables.c
 *
 * State atoms which upload the "binding table" for each shader stage.
 *
 * Binding tables map a numeric "surface index" to the SURFACE_STATE structure
 * for a currently bound surface.  This allows SEND messages (such as sampler
 * or data port messages) to refer to a particular surface by number, rather
 * than by pointer.
 *
 * The binding table is stored as a (sparse) array of SURFACE_STATE entries;
 * surface indexes are simply indexes into the array.  The ordering of the
 * entries is entirely left up to software; see the SURF_INDEX_* macros in
 * brw_context.h to see our current layout.
 */

#include "main/mtypes.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"
#include "intel_batchbuffer.h"

static const GLuint stage_to_bt_edit[] = {
   [MESA_SHADER_VERTEX] = _3DSTATE_BINDING_TABLE_EDIT_VS,
   [MESA_SHADER_GEOMETRY] = _3DSTATE_BINDING_TABLE_EDIT_GS,
   [MESA_SHADER_FRAGMENT] = _3DSTATE_BINDING_TABLE_EDIT_PS,
};

static uint32_t
reserve_hw_bt_space(struct brw_context *brw, unsigned bytes)
{
   /* From the Broadwell PRM, Volume 16, "Workarounds",
    * WaStateBindingTableOverfetch:
    * "HW over-fetches two cache lines of binding table indices.  When
    *  using the resource streamer, SW needs to pad binding table pointer
    *  updates with an additional two cache lines."
    *
    * Cache lines are 64 bytes, so we subtract 128 bytes from the size of
    * the binding table pool buffer.
    */
   if (brw->hw_bt_pool.next_offset + bytes >= brw->hw_bt_pool.bo->size - 128) {
      gen7_reset_hw_bt_pool_offsets(brw);
   }

   uint32_t offset = brw->hw_bt_pool.next_offset;

   /* From the Haswell PRM, Volume 2b: Command Reference: Instructions,
    * 3DSTATE_BINDING_TABLE_POINTERS_xS:
    *
    * "If HW Binding Table is enabled, the offset is relative to the
    *  Binding Table Pool Base Address and the alignment is 64 bytes."
    */
   brw->hw_bt_pool.next_offset += ALIGN(bytes, 64);

   return offset;
}

/**
 * Upload a shader stage's binding table as indirect state.
 *
 * This copies brw_stage_state::surf_offset[] into the indirect state section
 * of the batchbuffer (allocated by brw_state_batch()).
 */
void
brw_upload_binding_table(struct brw_context *brw,
                         uint32_t packet_name,
                         const struct brw_stage_prog_data *prog_data,
                         struct brw_stage_state *stage_state)
{
   if (prog_data->binding_table.size_bytes == 0) {
      /* There are no surfaces; skip making the binding table altogether. */
      if (stage_state->bind_bo_offset == 0 && brw->gen < 9)
         return;

      stage_state->bind_bo_offset = 0;
   } else {
      /* Upload a new binding table. */
      if (INTEL_DEBUG & DEBUG_SHADER_TIME) {
         brw_emit_buffer_surface_state(
            brw, &stage_state->surf_offset[
                    prog_data->binding_table.shader_time_start],
            brw->shader_time.bo, 0, BRW_SURFACEFORMAT_RAW,
            brw->shader_time.bo->size, 1, true);
      }
      /* When RS is enabled use hw-binding table uploads, otherwise fallback to
       * software-uploads.
       */
      if (brw->use_resource_streamer) {
         gen7_update_binding_table_from_array(brw, stage_state->stage,
                                              stage_state->surf_offset,
                                              prog_data->binding_table
                                              .size_bytes / 4);
      } else {
         uint32_t *bind = brw_state_batch(brw, AUB_TRACE_BINDING_TABLE,
                                          prog_data->binding_table.size_bytes,
                                          32,
                                          &stage_state->bind_bo_offset);

         /* BRW_NEW_SURFACES and BRW_NEW_*_CONSTBUF */
         memcpy(bind, stage_state->surf_offset,
                prog_data->binding_table.size_bytes);
      }
   }

   brw->ctx.NewDriverState |= BRW_NEW_BINDING_TABLE_POINTERS;

   if (brw->gen >= 7) {
      if (brw->use_resource_streamer) {
         stage_state->bind_bo_offset =
            reserve_hw_bt_space(brw, prog_data->binding_table.size_bytes);
      }
      BEGIN_BATCH(2);
      OUT_BATCH(packet_name << 16 | (2 - 2));
      /* Align SurfaceStateOffset[16:6] format to [15:5] PS Binding Table field
       * when hw-generated binding table is enabled.
       */
      OUT_BATCH(brw->use_resource_streamer ?
                (stage_state->bind_bo_offset >> 1) :
                stage_state->bind_bo_offset);
      ADVANCE_BATCH();
   }
}

/**
 * State atoms which upload the binding table for a particular shader stage.
 *  @{
 */

/** Upload the VS binding table. */
static void
brw_vs_upload_binding_table(struct brw_context *brw)
{
   /* BRW_NEW_VS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->vs.base.prog_data;
   brw_upload_binding_table(brw,
                            _3DSTATE_BINDING_TABLE_POINTERS_VS,
                            prog_data,
                            &brw->vs.base);
}

const struct brw_tracked_state brw_vs_binding_table = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_VS_CONSTBUF |
             BRW_NEW_VS_PROG_DATA |
             BRW_NEW_SURFACES,
   },
   .emit = brw_vs_upload_binding_table,
};


/** Upload the PS binding table. */
static void
brw_upload_wm_binding_table(struct brw_context *brw)
{
   /* BRW_NEW_FS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->wm.base.prog_data;
   brw_upload_binding_table(brw,
                            _3DSTATE_BINDING_TABLE_POINTERS_PS,
                            prog_data,
                            &brw->wm.base);
}

const struct brw_tracked_state brw_wm_binding_table = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_FS_PROG_DATA |
             BRW_NEW_SURFACES,
   },
   .emit = brw_upload_wm_binding_table,
};

/** Upload the TCS binding table (if tessellation stages are active). */
static void
brw_tcs_upload_binding_table(struct brw_context *brw)
{
   /* Skip if the tessellation stages are disabled. */
   if (brw->tess_eval_program == NULL)
      return;

   /* BRW_NEW_TCS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->tcs.base.prog_data;
   brw_upload_binding_table(brw,
                            _3DSTATE_BINDING_TABLE_POINTERS_HS,
                            prog_data,
                            &brw->tcs.base);
}

const struct brw_tracked_state brw_tcs_binding_table = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_DEFAULT_TESS_LEVELS |
             BRW_NEW_SURFACES |
             BRW_NEW_TCS_CONSTBUF |
             BRW_NEW_TCS_PROG_DATA,
   },
   .emit = brw_tcs_upload_binding_table,
};

/** Upload the TES binding table (if TES is active). */
static void
brw_tes_upload_binding_table(struct brw_context *brw)
{
   /* If there's no TES, skip changing anything. */
   if (brw->tess_eval_program == NULL)
      return;

   /* BRW_NEW_TES_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->tes.base.prog_data;
   brw_upload_binding_table(brw,
                            _3DSTATE_BINDING_TABLE_POINTERS_DS,
                            prog_data,
                            &brw->tes.base);
}

const struct brw_tracked_state brw_tes_binding_table = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_SURFACES |
             BRW_NEW_TES_CONSTBUF |
             BRW_NEW_TES_PROG_DATA,
   },
   .emit = brw_tes_upload_binding_table,
};

/** Upload the GS binding table (if GS is active). */
static void
brw_gs_upload_binding_table(struct brw_context *brw)
{
   /* If there's no GS, skip changing anything. */
   if (brw->geometry_program == NULL)
      return;

   /* BRW_NEW_GS_PROG_DATA */
   const struct brw_stage_prog_data *prog_data = brw->gs.base.prog_data;
   brw_upload_binding_table(brw,
                            _3DSTATE_BINDING_TABLE_POINTERS_GS,
                            prog_data,
                            &brw->gs.base);
}

const struct brw_tracked_state brw_gs_binding_table = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_GS_CONSTBUF |
             BRW_NEW_GS_PROG_DATA |
             BRW_NEW_SURFACES,
   },
   .emit = brw_gs_upload_binding_table,
};

/**
 * Edit a single entry in a hardware-generated binding table
 */
void
gen7_edit_hw_binding_table_entry(struct brw_context *brw,
                                 gl_shader_stage stage,
                                 uint32_t index,
                                 uint32_t surf_offset)
{
   assert(stage < ARRAY_SIZE(stage_to_bt_edit));
   assert(stage_to_bt_edit[stage]);

   uint32_t dw2 = SET_FIELD(index, BRW_BINDING_TABLE_INDEX) |
      (brw->gen >= 8 ? GEN8_SURFACE_STATE_EDIT(surf_offset) :
       HSW_SURFACE_STATE_EDIT(surf_offset));

   BEGIN_BATCH(3);
   OUT_BATCH(stage_to_bt_edit[stage] << 16 | (3 - 2));
   OUT_BATCH(BRW_BINDING_TABLE_EDIT_TARGET_ALL);
   OUT_BATCH(dw2);
   ADVANCE_BATCH();
}

/**
 * Upload a whole hardware binding table for the given stage.
 *
 * Takes an array of surface offsets and the number of binding table
 * entries.
 */
void
gen7_update_binding_table_from_array(struct brw_context *brw,
                                     gl_shader_stage stage,
                                     const uint32_t* binding_table,
                                     int num_surfaces)
{
   uint32_t dw2 = 0;

   assert(stage < ARRAY_SIZE(stage_to_bt_edit));
   assert(stage_to_bt_edit[stage]);

   BEGIN_BATCH(num_surfaces + 2);
   OUT_BATCH(stage_to_bt_edit[stage] << 16 | num_surfaces);
   OUT_BATCH(BRW_BINDING_TABLE_EDIT_TARGET_ALL);
   for (int i = 0; i < num_surfaces; i++) {
      dw2 = SET_FIELD(i, BRW_BINDING_TABLE_INDEX) |
         (brw->gen >= 8 ? GEN8_SURFACE_STATE_EDIT(binding_table[i]) :
          HSW_SURFACE_STATE_EDIT(binding_table[i]));
      OUT_BATCH(dw2);
   }
   ADVANCE_BATCH();
}

/**
 * Disable hardware binding table support, falling back to the
 * older software-generated binding table mechanism.
 */
void
gen7_disable_hw_binding_tables(struct brw_context *brw)
{
   if (!brw->use_resource_streamer)
      return;
   /* From the Haswell PRM, Volume 7: 3D Media GPGPU,
    * 3DSTATE_BINDING_TABLE_POOL_ALLOC > Programming Note:
    *
    * "When switching between HW and SW binding table generation, SW must
    * issue a state cache invalidate."
    */
   brw_emit_pipe_control_flush(brw, PIPE_CONTROL_STATE_CACHE_INVALIDATE);

   int pkt_len = brw->gen >= 8 ? 4 : 3;

   BEGIN_BATCH(pkt_len);
   OUT_BATCH(_3DSTATE_BINDING_TABLE_POOL_ALLOC << 16 | (pkt_len - 2));
   if (brw->gen >= 8) {
      OUT_BATCH(0);
      OUT_BATCH(0);
      OUT_BATCH(0);
   } else {
      OUT_BATCH(HSW_BT_POOL_ALLOC_MUST_BE_ONE);
      OUT_BATCH(0);
   }
   ADVANCE_BATCH();
}

/**
 * Enable hardware binding tables and set up the binding table pool.
 */
void
gen7_enable_hw_binding_tables(struct brw_context *brw)
{
   if (!brw->use_resource_streamer)
      return;

   if (!brw->hw_bt_pool.bo) {
      /* We use a single re-usable buffer object for the lifetime of the
       * context and size it to maximum allowed binding tables that can be
       * programmed per batch:
       *
       * From the Haswell PRM, Volume 7: 3D Media GPGPU,
       * 3DSTATE_BINDING_TABLE_POOL_ALLOC > Programming Note:
       * "A maximum of 16,383 Binding tables are allowed in any batch buffer"
       */
      static const int max_size = 16383 * 4;
      brw->hw_bt_pool.bo = drm_intel_bo_alloc(brw->bufmgr, "hw_bt",
                                              max_size, 64);
      brw->hw_bt_pool.next_offset = 0;
   }

   /* From the Haswell PRM, Volume 7: 3D Media GPGPU,
    * 3DSTATE_BINDING_TABLE_POOL_ALLOC > Programming Note:
    *
    * "When switching between HW and SW binding table generation, SW must
    * issue a state cache invalidate."
    */
   brw_emit_pipe_control_flush(brw, PIPE_CONTROL_STATE_CACHE_INVALIDATE);

   int pkt_len = brw->gen >= 8 ? 4 : 3;
   uint32_t dw1 = BRW_HW_BINDING_TABLE_ENABLE;
   if (brw->is_haswell) {
      dw1 |= SET_FIELD(GEN7_MOCS_L3, GEN7_HW_BT_POOL_MOCS) |
             HSW_BT_POOL_ALLOC_MUST_BE_ONE;
   } else if (brw->gen >= 8) {
      dw1 |= BDW_MOCS_WB;
   }

   BEGIN_BATCH(pkt_len);
   OUT_BATCH(_3DSTATE_BINDING_TABLE_POOL_ALLOC << 16 | (pkt_len - 2));
   if (brw->gen >= 8) {
      OUT_RELOC64(brw->hw_bt_pool.bo, I915_GEM_DOMAIN_SAMPLER, 0, dw1);
      OUT_BATCH(brw->hw_bt_pool.bo->size);
   } else {
      OUT_RELOC(brw->hw_bt_pool.bo, I915_GEM_DOMAIN_SAMPLER, 0, dw1);
      OUT_RELOC(brw->hw_bt_pool.bo, I915_GEM_DOMAIN_SAMPLER, 0,
             brw->hw_bt_pool.bo->size);
   }
   ADVANCE_BATCH();
}

void
gen7_reset_hw_bt_pool_offsets(struct brw_context *brw)
{
   brw->hw_bt_pool.next_offset = 0;
}

const struct brw_tracked_state gen7_hw_binding_tables = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP,
   },
   .emit = gen7_enable_hw_binding_tables
};

/** @} */

/**
 * State atoms which emit 3DSTATE packets to update the binding table pointers.
 *  @{
 */

/**
 * (Gen4-5) Upload the binding table pointers for all shader stages.
 *
 * The binding table pointers are relative to the surface state base address,
 * which points at the batchbuffer containing the streamed batch state.
 */
static void
gen4_upload_binding_table_pointers(struct brw_context *brw)
{
   BEGIN_BATCH(6);
   OUT_BATCH(_3DSTATE_BINDING_TABLE_POINTERS << 16 | (6 - 2));
   OUT_BATCH(brw->vs.base.bind_bo_offset);
   OUT_BATCH(0); /* gs */
   OUT_BATCH(0); /* clip */
   OUT_BATCH(0); /* sf */
   OUT_BATCH(brw->wm.base.bind_bo_offset);
   ADVANCE_BATCH();
}

const struct brw_tracked_state brw_binding_table_pointers = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_BINDING_TABLE_POINTERS |
             BRW_NEW_STATE_BASE_ADDRESS,
   },
   .emit = gen4_upload_binding_table_pointers,
};

/**
 * (Sandybridge Only) Upload the binding table pointers for all shader stages.
 *
 * The binding table pointers are relative to the surface state base address,
 * which points at the batchbuffer containing the streamed batch state.
 */
static void
gen6_upload_binding_table_pointers(struct brw_context *brw)
{
   BEGIN_BATCH(4);
   OUT_BATCH(_3DSTATE_BINDING_TABLE_POINTERS << 16 |
             GEN6_BINDING_TABLE_MODIFY_VS |
             GEN6_BINDING_TABLE_MODIFY_GS |
             GEN6_BINDING_TABLE_MODIFY_PS |
             (4 - 2));
   OUT_BATCH(brw->vs.base.bind_bo_offset); /* vs */
   if (brw->ff_gs.prog_active)
      OUT_BATCH(brw->ff_gs.bind_bo_offset); /* gs */
   else
      OUT_BATCH(brw->gs.base.bind_bo_offset); /* gs */
   OUT_BATCH(brw->wm.base.bind_bo_offset); /* wm/ps */
   ADVANCE_BATCH();
}

const struct brw_tracked_state gen6_binding_table_pointers = {
   .dirty = {
      .mesa = 0,
      .brw = BRW_NEW_BATCH |
             BRW_NEW_BLORP |
             BRW_NEW_BINDING_TABLE_POINTERS |
             BRW_NEW_STATE_BASE_ADDRESS,
   },
   .emit = gen6_upload_binding_table_pointers,
};

/** @} */
