/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_build_pm4.h"
#include "si_shader_internal.h"
#include "si_query.h"
#include "nir.h"
#include "util/u_upload_mgr.h"

#define SI_MESH_PIPELINE_STATE_DIRTY_MASK \
   (BITFIELD_BIT(MESA_SHADER_TASK) | \
    BITFIELD_BIT(MESA_SHADER_MESH) | \
    BITFIELD_BIT(MESA_SHADER_FRAGMENT) | \
    SI_SQTT_STATE_DIRTY_BIT)

static void *si_create_ts_state(struct pipe_context *ctx,
                                const struct pipe_shader_state *state)
{
   assert(state->type == PIPE_SHADER_IR_NIR);
   return si_create_compute_state_for_nir(ctx, state->ir.nir, MESA_SHADER_TASK);
}

static void si_bind_ts_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_compute *program = (struct si_compute *)state;
   struct si_shader_selector *sel = &program->sel;

   sctx->ts_shader_state.program = program;
   if (!program)
      return;

   /* Wait because we need active slot usage masks. */
   util_queue_fence_wait(&sel->ready);

   si_update_common_shader_state(sctx, sel, MESA_SHADER_TASK);
}

static void si_delete_ts_state(struct pipe_context *ctx, void *state)
{
   struct si_compute *task = (struct si_compute *)state;
   struct si_context *sctx = (struct si_context *)ctx;

   if (!state)
      return;

   if (task == sctx->ts_shader_state.program)
      sctx->ts_shader_state.program = NULL;

   if (task == sctx->ts_shader_state.emitted_program)
      sctx->ts_shader_state.emitted_program = NULL;

   si_compute_reference(&task, NULL);
}

static bool si_init_mesh_scratch_ring(struct si_context *sctx)
{
   if (!sctx->mesh_scratch_ring) {
      sctx->mesh_scratch_ring = si_aligned_buffer_create(
         &sctx->screen->b, SI_RESOURCE_FLAG_DRIVER_INTERNAL | SI_RESOURCE_FLAG_32BIT,
         PIPE_USAGE_DEFAULT, AC_MESH_SCRATCH_NUM_ENTRIES * AC_MESH_SCRATCH_ENTRY_BYTES,
         256);

      if (!sctx->mesh_scratch_ring) {
         fprintf(stderr, "radeonsi: can't create mesh scratch ring\n");
         return false;
      }
   }

   radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->mesh_scratch_ring,
                             RADEON_USAGE_READWRITE | RADEON_PRIO_SHADER_RINGS);

   return true;
}

static bool si_update_mesh_shader(struct si_context *sctx)
{
   bool is_ms_state_changed =
      (sctx->dirty_shaders_mask & BITFIELD_BIT(MESA_SHADER_MESH)) != 0;

   struct si_shader *old_ms = sctx->ms_shader_state.current;

   si_pm4_bind_state(sctx, hs, NULL);

   if (sctx->gfx_level < GFX11)
      si_pm4_bind_state(sctx, vs, NULL);

   if (is_ms_state_changed) {
      int r = si_shader_select(&sctx->b, &sctx->ms_shader_state);
      if (r)
         return false;
      si_pm4_bind_state(sctx, gs, sctx->ms_shader_state.current);
   }

   struct si_shader *new_ms = sctx->ms_shader_state.current;

   if (!si_update_shaders_for_mesh(sctx, old_ms, new_ms))
      return false;

   if (si_pm4_state_enabled_and_changed(sctx, gs)) {
      if (sctx->ms_shader_state.current->info.uses_mesh_scratch_ring) {
         if (!si_init_mesh_scratch_ring(sctx))
            return false;
      }
   }

   sctx->dirty_shaders_mask &= ~SI_MESH_PIPELINE_STATE_DIRTY_MASK;
   return true;
}

static void si_emit_prim_state(struct si_context *sctx)
{
   si_emit_rasterizer_prim_state_for_mesh(sctx);

   radeon_begin(&sctx->gfx_cs);
   if (sctx->last_prim != MESA_PRIM_POINTS) {
      radeon_set_uconfig_reg(R_030908_VGT_PRIMITIVE_TYPE, V_008958_DI_PT_POINTLIST);
      sctx->last_prim = MESA_PRIM_POINTS;
   }
   radeon_end();
}

#define set_task_sh_reg(reg, value) \
   do { \
      if (sctx->gfx_level >= GFX12) \
         gfx12_push_compute_sh_reg(reg, value); \
      else if (sctx->screen->info.has_set_sh_pairs_packed) \
         gfx11_push_compute_sh_reg(reg, value); \
      else \
         radeon_set_sh_reg(reg, value); \
   } while (0)

static void si_emit_draw_mesh_tasks_ace_packets(struct si_context *sctx,
                                                const struct pipe_grid_info *info,
                                                bool prefetch)
{
   struct radeon_cmdbuf *cs = sctx->gfx_cs.gang_cs;
   struct si_shader *shader = &sctx->ts_shader_state.program->shader;
   bool uses_draw_id = shader->info.uses_draw_id;
   bool uses_grid_size = shader->selector->info.uses_grid_size;
   unsigned sh_base_reg = R_00B900_COMPUTE_USER_DATA_0;

   unsigned reg = sh_base_reg + 4 * GFX10_SGPR_TS_TASK_RING_ENTRY;
   unsigned ring_entry_loc = (reg - SI_SH_REG_OFFSET) >> 2;
   reg += 4;

   unsigned draw_id_reg = 0;
   unsigned grid_size_reg = 0;
   if (uses_draw_id) {
      draw_id_reg = reg;
      reg += 4;
   }
   if (uses_grid_size)
      grid_size_reg = reg;

   unsigned dispatch_initiator =
      S_00B800_COMPUTE_SHADER_EN(1) |
      S_00B800_DISABLE_DISP_PREMPT_EN(1) |
      S_00B800_ORDER_MODE(1) |
      S_00B800_TUNNEL_ENABLE(1) |
      S_00B800_CS_W32_EN(shader->wave_size == 32);

   if (info->indirect) {
      si_emit_buffered_compute_sh_regs(sctx, cs);

      if (prefetch)
         si_cp_dma_prefetch(cs, sctx->gfx_level, &shader->bo->b.b, 0,
                            shader->bo->b.b.width0);

      uint64_t data_va =
         si_resource(info->indirect)->gpu_address + info->indirect_offset;

      uint64_t count_va = info->indirect_draw_count ?
         si_resource(info->indirect_draw_count)->gpu_address +
         info->indirect_draw_count_offset : 0;

      unsigned draw_id_loc =
         uses_draw_id ? (draw_id_reg - SI_SH_REG_OFFSET) >> 2 : 0;
      unsigned grid_size_loc =
         uses_grid_size ? (grid_size_reg - SI_SH_REG_OFFSET) >> 2 : 0;

      radeon_begin(cs);

      radeon_emit(PKT3(PKT3_DISPATCH_TASKMESH_INDIRECT_MULTI_ACE, 9, 0) |
                  PKT3_SHADER_TYPE_S(1));
      radeon_emit(data_va);
      radeon_emit(data_va >> 32);
      radeon_emit(S_AD2_RING_ENTRY_REG(ring_entry_loc));
      radeon_emit(S_AD3_COUNT_INDIRECT_ENABLE(!!count_va) |
                  S_AD3_DRAW_INDEX_ENABLE(uses_draw_id) |
                  S_AD3_XYZ_DIM_ENABLE(uses_grid_size) |
                  S_AD3_DRAW_INDEX_REG(draw_id_loc));
      radeon_emit(S_AD4_XYZ_DIM_REG(grid_size_loc));
      radeon_emit(info->draw_count);
      radeon_emit(count_va);
      radeon_emit(count_va >> 32);
      radeon_emit(info->indirect_stride);
      radeon_emit(dispatch_initiator);

      radeon_end();
   } else {
      radeon_begin(cs);

      if (uses_draw_id)
         set_task_sh_reg(draw_id_reg, 0);
      if (uses_grid_size) {
         set_task_sh_reg(grid_size_reg, info->grid[0]);
         set_task_sh_reg(grid_size_reg + 4, info->grid[1]);
         set_task_sh_reg(grid_size_reg + 8, info->grid[2]);
      }

      radeon_end();

      si_emit_buffered_compute_sh_regs(sctx, cs);

      if (prefetch)
         si_cp_dma_prefetch(cs, sctx->gfx_level, &shader->bo->b.b, 0,
                            shader->bo->b.b.width0);

      radeon_begin_again(cs);

      radeon_emit(PKT3(PKT3_DISPATCH_TASKMESH_DIRECT_ACE, 4, sctx->render_cond_enabled) |
                  PKT3_SHADER_TYPE_S(1));
      radeon_emit(info->grid[0]);
      radeon_emit(info->grid[1]);
      radeon_emit(info->grid[2]);
      radeon_emit(dispatch_initiator);
      radeon_emit(ring_entry_loc & 0xFFFF);

      radeon_end();
   }
}

#define radeon_emit_alt_hiz_logic() \
   do { \
      if (sctx->gfx_level == GFX12 && sctx->screen->options.alt_hiz_logic) \
         radeon_emit_alt_hiz_packets(); \
   } while (0)

static void clear_reg_saved_mask(struct si_context *sctx, unsigned reg)
{
   if (reg >= SI_SGPR_BASE_VERTEX && reg <= SI_SGPR_START_INSTANCE) {
      BITSET_CLEAR(sctx->tracked_regs.reg_saved_mask,
                   SI_TRACKED_SPI_SHADER_USER_DATA_ES__BASE_VERTEX +
                   (reg - SI_SGPR_BASE_VERTEX));
   }
}

#define set_mesh_sh_reg(reg, value) \
   do { \
      unsigned addr = sh_base_reg + (reg) * 4; \
      if ((reg) >= SI_SGPR_BASE_VERTEX && (reg) <= SI_SGPR_START_INSTANCE) { \
         unsigned tracked_reg = SI_TRACKED_SPI_SHADER_USER_DATA_ES__BASE_VERTEX; \
         tracked_reg += (reg) - SI_SGPR_BASE_VERTEX; \
         if (sctx->gfx_level >= GFX12) \
            gfx12_opt_push_gfx_sh_reg(addr, tracked_reg, value); \
         else if (sctx->screen->info.has_set_sh_pairs_packed) \
            gfx11_opt_push_gfx_sh_reg(addr, tracked_reg, value); \
         else \
            radeon_opt_set_sh_reg(addr, tracked_reg, value); \
      } else { \
         if (sctx->gfx_level >= GFX12) \
            gfx12_push_gfx_sh_reg(addr, value); \
         else if (sctx->screen->info.has_set_sh_pairs_packed) \
            gfx11_push_gfx_sh_reg(addr, value); \
         else \
            radeon_set_sh_reg(addr, value); \
      } \
   } while (0)

static void si_emit_draw_mesh_tasks_gfx_packets(struct si_context *sctx,
                                                const struct pipe_grid_info *info)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned sh_base_reg = sctx->shader_pointers.sh_base[MESA_SHADER_MESH];
   struct si_shader *shader = sctx->ms_shader_state.current;
   struct si_shader_selector *sel = shader->selector;
   bool uses_grid_size = sel->info.uses_grid_size;

   int offset = GFX11_SGPR_MS_ATTRIBUTE_RING_ADDR;
   if (sctx->gfx_level >= GFX11)
      offset++;
   unsigned ring_entry_reg = offset;
   offset++;
   unsigned task_ring_addr_reg = 0;
   if (sel->info.base.task_payload_size) {
      task_ring_addr_reg = offset;
      offset++;
   }
   /* mesh shader after task shader should not use gl_DrawID */
   assert(!shader->info.uses_draw_id);
   unsigned grid_size_reg = 0;
   if (uses_grid_size || sctx->gfx_level < GFX11) {
      grid_size_reg = offset;
      offset += 3;
   }
   unsigned mesh_scratch_ring_addr_reg = 0;
   if (shader->info.uses_mesh_scratch_ring) {
      mesh_scratch_ring_addr_reg = offset;
      offset++;
   }

   radeon_begin(cs);

   if (task_ring_addr_reg)
      set_mesh_sh_reg(task_ring_addr_reg, sctx->task_ring->gpu_address);
   if (mesh_scratch_ring_addr_reg)
      set_mesh_sh_reg(mesh_scratch_ring_addr_reg, sctx->mesh_scratch_ring->gpu_address);

   radeon_end();
   si_emit_buffered_gfx_sh_regs_for_mesh(sctx);
   radeon_begin_again(cs);

   /* Invalidate tracked draw constants because DispatchTaskMeshGFX overwrites them. */
   clear_reg_saved_mask(sctx, ring_entry_reg);
   if (grid_size_reg) {
      for (unsigned i = 0; i < 3; i++)
         clear_reg_saved_mask(sctx, grid_size_reg + i);
   }

   unsigned grid_size_loc = grid_size_reg ?
      (sh_base_reg + grid_size_reg * 4 - SI_SH_REG_OFFSET) >> 2 : 0;
   unsigned ring_entry_loc =
      (sh_base_reg + ring_entry_reg * 4 - SI_SH_REG_OFFSET) >> 2;

   bool linear_taskmesh_dispatch =
      sctx->ts_shader_state.program->sel.info.base.task.linear_taskmesh_dispatch;

   radeon_emit(PKT3(PKT3_DISPATCH_TASKMESH_GFX, 2, sctx->render_cond_enabled) | PKT3_RESET_FILTER_CAM_S(1));
   radeon_emit(S_4D0_RING_ENTRY_REG(ring_entry_loc) | S_4D0_XYZ_DIM_REG(grid_size_loc));
   if (sctx->gfx_level >= GFX11)
      radeon_emit(S_4D1_XYZ_DIM_ENABLE(uses_grid_size) |
                  S_4D1_MODE1_ENABLE(!sctx->screen->info.mesh_fast_launch_2) |
                  S_4D1_LINEAR_DISPATCH_ENABLE(linear_taskmesh_dispatch));
   else
      radeon_emit(0);
   radeon_emit(V_0287F0_DI_SRC_SEL_AUTO_INDEX);

   radeon_emit_alt_hiz_logic();

   radeon_end();

   sctx->last_instance_count = SI_INSTANCE_COUNT_UNKNOWN;
}

static void si_emit_draw_mesh_shader_only_packets(struct si_context *sctx,
                                                  const struct pipe_grid_info *info)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_shader *shader = sctx->ms_shader_state.current;
   struct si_shader_selector *sel = shader->selector;
   bool uses_draw_id = shader->info.uses_draw_id;
   bool uses_grid_size = sel->info.uses_grid_size;
   unsigned sh_base_reg = sctx->shader_pointers.sh_base[MESA_SHADER_MESH];

   int offset = GFX11_SGPR_MS_ATTRIBUTE_RING_ADDR;
   if (sctx->gfx_level >= GFX11)
      offset++;
   /* task ring entry */
   offset++;
   /* mesh shader only case has no payload */
   assert(!sel->info.base.task_payload_size);
   unsigned draw_id_reg = 0;
   if (uses_draw_id) {
      draw_id_reg = offset;
      offset++;
   }
   unsigned grid_size_reg = 0;
   if (uses_grid_size || sctx->gfx_level < GFX11) {
      grid_size_reg = offset;
      offset += 3;
   }
   unsigned mesh_scratch_ring_addr_reg = 0;
   if (shader->info.uses_mesh_scratch_ring) {
      mesh_scratch_ring_addr_reg = offset;
      offset++;
   }

   radeon_begin(cs);

   if (mesh_scratch_ring_addr_reg)
      set_mesh_sh_reg(mesh_scratch_ring_addr_reg, sctx->mesh_scratch_ring->gpu_address);

   if (info->indirect) {
      sctx->last_instance_count = SI_INSTANCE_COUNT_UNKNOWN;

      /* Invalidate tracked draw constants because DispatchMeshIndirect overwrites them. */
      if (draw_id_reg)
         clear_reg_saved_mask(sctx, draw_id_reg);
      if (grid_size_reg) {
         for (unsigned i = 0; i < 3; i++)
            clear_reg_saved_mask(sctx, grid_size_reg + i);
      }

      radeon_end();
      si_emit_buffered_gfx_sh_regs_for_mesh(sctx);
      radeon_begin_again(cs);

      uint64_t count_va = info->indirect_draw_count ?
         si_resource(info->indirect_draw_count)->gpu_address +
         info->indirect_draw_count_offset : 0;

      uint64_t base_va = si_resource(info->indirect)->gpu_address;
      radeon_emit(PKT3(PKT3_SET_BASE, 2, 0));
      radeon_emit(1);
      radeon_emit(base_va);
      radeon_emit(base_va >> 32);

      unsigned draw_id_loc = draw_id_reg ?
         (sh_base_reg + draw_id_reg * 4 - SI_SH_REG_OFFSET) >> 2 : 0;
      unsigned grid_size_loc = grid_size_reg ?
         (sh_base_reg + grid_size_reg * 4 - SI_SH_REG_OFFSET) >> 2 : 0;

      radeon_emit(PKT3(PKT3_DISPATCH_MESH_INDIRECT_MULTI, 7, sctx->render_cond_enabled));
      radeon_emit(info->indirect_offset);
      radeon_emit(S_4C1_XYZ_DIM_REG(grid_size_loc) | S_4C1_DRAW_INDEX_REG(draw_id_loc));
      if (sctx->gfx_level >= GFX11)
         radeon_emit(S_4C2_DRAW_INDEX_ENABLE(uses_draw_id) |
                     S_4C2_COUNT_INDIRECT_ENABLE(!!count_va) |
                     S_4C2_XYZ_DIM_ENABLE(uses_grid_size) |
                     S_4C2_MODE1_ENABLE(!sctx->screen->info.mesh_fast_launch_2));
      else
         radeon_emit(S_4C2_DRAW_INDEX_ENABLE(uses_draw_id) |
                     S_4C2_COUNT_INDIRECT_ENABLE(!!count_va));
      radeon_emit(info->draw_count);
      radeon_emit(count_va);
      radeon_emit(count_va >> 32);
      radeon_emit(info->indirect_stride);
      radeon_emit(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
   } else {
      if (draw_id_reg)
         set_mesh_sh_reg(draw_id_reg, 0);
      if (grid_size_reg) {
         set_mesh_sh_reg(grid_size_reg, info->grid[0]);
         set_mesh_sh_reg(grid_size_reg + 1, info->grid[1]);
         set_mesh_sh_reg(grid_size_reg + 2, info->grid[2]);
      }

      radeon_end();
      si_emit_buffered_gfx_sh_regs_for_mesh(sctx);
      radeon_begin_again(cs);

      if (sctx->screen->info.mesh_fast_launch_2) {
         radeon_emit(PKT3(PKT3_DISPATCH_MESH_DIRECT, 3, sctx->render_cond_enabled));
         radeon_emit(info->grid[0]);
         radeon_emit(info->grid[1]);
         radeon_emit(info->grid[2]);
         radeon_emit(S_0287F0_SOURCE_SELECT(V_0287F0_DI_SRC_SEL_AUTO_INDEX));

         sctx->last_instance_count = SI_INSTANCE_COUNT_UNKNOWN;
      } else {
         if (sctx->last_instance_count != 1) {
            radeon_emit(PKT3(PKT3_NUM_INSTANCES, 0, 0));
            radeon_emit(1);
            sctx->last_instance_count = 1;
         }

         radeon_emit(PKT3(PKT3_DRAW_INDEX_AUTO, 1, sctx->render_cond_enabled));
         radeon_emit(info->grid[0] * info->grid[1] * info->grid[2]);
         radeon_emit(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
      }
   }

   radeon_emit_alt_hiz_logic();
   radeon_end();
}

static void si_prefetch_mesh_shaders(struct si_context *sctx)
{
   unsigned mask = sctx->prefetch_L2_mask;

   if (mask & SI_PREFETCH_GS) {
      struct si_shader *shader = sctx->queued.named.gs;
      si_cp_dma_prefetch(&sctx->gfx_cs, sctx->gfx_level, &shader->bo->b.b,
                         0, shader->bo->b.b.width0);
   }

   if (mask & SI_PREFETCH_PS) {
      struct si_shader *shader = sctx->queued.named.ps;
      si_cp_dma_prefetch(&sctx->gfx_cs, sctx->gfx_level, &shader->bo->b.b,
                         0, shader->bo->b.b.width0);
   }

   sctx->prefetch_L2_mask = 0;
}

static void si_set_task_tmpring_size(struct si_context *sctx, unsigned bytes_per_wave)
{
   bytes_per_wave = ac_compute_scratch_wavesize(&sctx->screen->info, bytes_per_wave);

   sctx->max_seen_task_scratch_bytes_per_wave =
      MAX2(sctx->max_seen_task_scratch_bytes_per_wave, bytes_per_wave);

   ac_get_scratch_tmpring_size(&sctx->screen->info,
                               sctx->screen->info.max_scratch_waves,
                               sctx->max_seen_task_scratch_bytes_per_wave,
                               &sctx->task_tmpring_size);
}

static void si_create_task_preamble_state(struct si_context *sctx)
{
   struct si_pm4_state *pm4 = si_pm4_create_sized(sctx->screen, 64, false);
   if (!pm4)
      return;

   si_init_compute_preamble_state(sctx, pm4);

   ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_EVENT_WRITE, 0, 0));
   ac_pm4_cmd_add(&pm4->base, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH) | EVENT_INDEX(4));

   ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_DISPATCH_TASK_STATE_INIT, 1, 0) | PKT3_SHADER_TYPE_S(1));
   ac_pm4_cmd_add(&pm4->base, sctx->task_ring->gpu_address & 0xFFFFFF00);
   ac_pm4_cmd_add(&pm4->base, sctx->task_ring->gpu_address >> 32);

   ac_pm4_set_reg(&pm4->base,
                  R_00B900_COMPUTE_USER_DATA_0 + GFX10_SGPR_TS_TASK_RING_ADDR * 4,
                  sctx->task_ring->gpu_address);

   ac_pm4_set_reg(&pm4->base, R_00B810_COMPUTE_START_X, 0);
   ac_pm4_set_reg(&pm4->base, R_00B814_COMPUTE_START_Y, 0);
   ac_pm4_set_reg(&pm4->base, R_00B818_COMPUTE_START_Z, 0);

   ac_pm4_finalize(&pm4->base);
   sctx->task_preamble_state = pm4;
}

static bool si_init_context_task_shader_states(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;
   struct radeon_winsys *ws = sscreen->ws;

   if (!sctx->gfx_cs.gang_cs) {
      if (!ws->cs_create_compute_gang(&sctx->gfx_cs)) {
         fprintf(stderr, "radeonsi: can't create task cs\n");
         return false;
      }
      si_set_task_tmpring_size(sctx, 0);
   }

   if (!sctx->task_ring) {
      sctx->task_ring = si_aligned_buffer_create(
         &sscreen->b,
         SI_RESOURCE_FLAG_DRIVER_INTERNAL |
         SI_RESOURCE_FLAG_32BIT |
         SI_RESOURCE_FLAG_CLEAR,
         PIPE_USAGE_DEFAULT,
         sscreen->task_info.bo_size_bytes, 256);

      if (!sctx->task_ring) {
         fprintf(stderr, "radeonsi: can't create task ring\n");
         return false;
      }

      uint32_t *ptr = (uint32_t *)ws->buffer_map(ws, sctx->task_ring->buf, NULL,
                                                 PIPE_MAP_WRITE);
      if (!ptr) {
         fprintf(stderr, "radeonsi: can't map task ring\n");
         si_resource_reference(&sctx->task_ring, NULL);
         return false;
      }

      const uint32_t num_entries = sscreen->task_info.num_entries;
      const uint64_t task_va = sctx->task_ring->gpu_address;
      const uint64_t task_draw_ring_va = task_va + sscreen->task_info.draw_ring_offset;
      assert((task_draw_ring_va & 0xFFull) == 0);

      /* 64-bit write_ptr */
      ptr[0] = num_entries;
      ptr[1] = 0;
      /* 64-bit read_ptr */
      ptr[2] = num_entries;
      ptr[3] = 0;
      /* 64-bit dealloc_ptr */
      ptr[4] = num_entries;
      ptr[5] = 0;
      /* num_entries */
      ptr[6] = num_entries;
      /* 64-bit draw ring address */
      ptr[7] = task_draw_ring_va;
      ptr[8] = task_draw_ring_va >> 32;
   }

   if (!sctx->task_wait_buf) {
      sctx->task_wait_buf =
         si_aligned_buffer_create(&sscreen->b,
                                  SI_RESOURCE_FLAG_DRIVER_INTERNAL,
                                  PIPE_USAGE_DEFAULT, 4,
                                  sscreen->info.tcc_cache_line_size);
      if (!sctx->task_wait_buf) {
         fprintf(stderr, "radeonsi: can't create task wait buffer\n");
         return false;
      }

      uint32_t *ptr = (uint32_t *)ws->buffer_map(ws, sctx->task_wait_buf->buf, NULL,
                                                 PIPE_MAP_WRITE);
      if (!ptr) {
         fprintf(stderr, "radeonsi: can't map task wait buffer\n");
         si_resource_reference(&sctx->task_wait_buf, NULL);
         return false;
      }

      *ptr = 0;
   }

   if (!sctx->task_preamble_state)
      si_create_task_preamble_state(sctx);

   return true;
}

static void si_emit_task_state_init_packet(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_emit(PKT3(PKT3_DISPATCH_TASK_STATE_INIT, 1, 0));
   radeon_emit(sctx->task_ring->gpu_address & 0xFFFFFF00);
   radeon_emit(sctx->task_ring->gpu_address >> 32);
   radeon_end();
}

static void si_emit_task_shader_packets(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = sctx->gfx_cs.gang_cs;
   struct si_shader *shader = &sctx->ts_shader_state.program->shader;
   const struct ac_shader_config *config = &shader->config;
   struct si_shader_info *sinfo = &shader->selector->info;
   uint16_t *workgroup_size = sinfo->base.workgroup_size;

   unsigned threads_per_threadgroup =
      workgroup_size[0] * workgroup_size[1] * workgroup_size[2];
   unsigned waves_per_threadgroup =
      DIV_ROUND_UP(threads_per_threadgroup, shader->wave_size);
   unsigned threadgroups_per_cu = waves_per_threadgroup == 1 ? 2 : 1;
   unsigned compute_resource_limits =
      ac_get_compute_resource_limits(&sctx->screen->info, waves_per_threadgroup,
                                     sctx->cs_max_waves_per_sh,
                                     threadgroups_per_cu);

   uint32_t num_threads[3];
   if (sctx->gfx_level >= GFX12) {
      num_threads[0] = S_00B81C_NUM_THREAD_FULL_GFX12(workgroup_size[0]);
      num_threads[1] = S_00B820_NUM_THREAD_FULL_GFX12(workgroup_size[1]);
   } else {
      num_threads[0] = S_00B81C_NUM_THREAD_FULL_GFX6(workgroup_size[0]);
      num_threads[1] = S_00B820_NUM_THREAD_FULL_GFX6(workgroup_size[1]);
   }
   num_threads[2] = S_00B824_NUM_THREAD_FULL(workgroup_size[2]);

   uint64_t shader_va = shader->bo->gpu_address;

   if (config->scratch_bytes_per_wave && !sctx->screen->info.has_scratch_base_registers)
      simple_mtx_unlock(&shader->selector->mutex);

   if (sctx->gfx_level >= GFX12) {
      gfx12_push_compute_sh_reg(R_00B830_COMPUTE_PGM_LO, shader_va >> 8);
      gfx12_push_compute_sh_reg(R_00B848_COMPUTE_PGM_RSRC1, config->rsrc1);
      gfx12_push_compute_sh_reg(R_00B84C_COMPUTE_PGM_RSRC2, config->rsrc2);
      gfx12_push_compute_sh_reg(R_00B8A0_COMPUTE_PGM_RSRC3, config->rsrc3);
      gfx12_push_compute_sh_reg(R_00B860_COMPUTE_TMPRING_SIZE, sctx->task_tmpring_size);

      if (config->scratch_bytes_per_wave) {
         gfx12_push_compute_sh_reg(R_00B840_COMPUTE_DISPATCH_SCRATCH_BASE_LO,
                                   sctx->task_scratch_buffer->gpu_address >> 8);
         gfx12_push_compute_sh_reg(R_00B844_COMPUTE_DISPATCH_SCRATCH_BASE_HI,
                                   sctx->task_scratch_buffer->gpu_address >> 40);
      }

      gfx12_push_compute_sh_reg(R_00B854_COMPUTE_RESOURCE_LIMITS,
                                compute_resource_limits);

      gfx12_push_compute_sh_reg(R_00B81C_COMPUTE_NUM_THREAD_X, num_threads[0]);
      gfx12_push_compute_sh_reg(R_00B820_COMPUTE_NUM_THREAD_Y, num_threads[1]);
      gfx12_push_compute_sh_reg(R_00B824_COMPUTE_NUM_THREAD_Z, num_threads[2]);
   } else if (sctx->screen->info.has_set_sh_pairs_packed) {
      gfx11_push_compute_sh_reg(R_00B830_COMPUTE_PGM_LO, shader_va >> 8);
      gfx11_push_compute_sh_reg(R_00B848_COMPUTE_PGM_RSRC1, config->rsrc1);
      gfx11_push_compute_sh_reg(R_00B84C_COMPUTE_PGM_RSRC2, config->rsrc2);
      gfx11_push_compute_sh_reg(R_00B8A0_COMPUTE_PGM_RSRC3, config->rsrc3);
      gfx11_push_compute_sh_reg(R_00B860_COMPUTE_TMPRING_SIZE, sctx->task_tmpring_size);

      if (config->scratch_bytes_per_wave) {
         gfx11_push_compute_sh_reg(R_00B840_COMPUTE_DISPATCH_SCRATCH_BASE_LO,
                                   sctx->task_scratch_buffer->gpu_address >> 8);
         gfx11_push_compute_sh_reg(R_00B844_COMPUTE_DISPATCH_SCRATCH_BASE_HI,
                                   sctx->task_scratch_buffer->gpu_address >> 40);
      }

      gfx11_push_compute_sh_reg(R_00B854_COMPUTE_RESOURCE_LIMITS,
                                compute_resource_limits);

      gfx11_push_compute_sh_reg(R_00B81C_COMPUTE_NUM_THREAD_X, num_threads[0]);
      gfx11_push_compute_sh_reg(R_00B820_COMPUTE_NUM_THREAD_Y, num_threads[1]);
      gfx11_push_compute_sh_reg(R_00B824_COMPUTE_NUM_THREAD_Z, num_threads[2]);
   } else {
      radeon_begin(cs);
      radeon_set_sh_reg(R_00B830_COMPUTE_PGM_LO, shader_va >> 8);
      radeon_set_sh_reg_seq(R_00B848_COMPUTE_PGM_RSRC1, 2);
      radeon_emit(config->rsrc1);
      radeon_emit(config->rsrc2);
      radeon_set_sh_reg(R_00B860_COMPUTE_TMPRING_SIZE, sctx->task_tmpring_size);
      radeon_set_sh_reg(R_00B8A0_COMPUTE_PGM_RSRC3, config->rsrc3);

      if (config->scratch_bytes_per_wave && sctx->screen->info.has_scratch_base_registers) {
         radeon_set_sh_reg_seq(R_00B840_COMPUTE_DISPATCH_SCRATCH_BASE_LO, 2);
         radeon_emit(sctx->task_scratch_buffer->gpu_address >> 8);
         radeon_emit(sctx->task_scratch_buffer->gpu_address >> 40);
      }

      radeon_set_sh_reg(R_00B854_COMPUTE_RESOURCE_LIMITS, compute_resource_limits);

      radeon_set_sh_reg_seq(R_00B81C_COMPUTE_NUM_THREAD_X, 3);
      radeon_emit(num_threads[0]);
      radeon_emit(num_threads[1]);
      radeon_emit(num_threads[2]);
      radeon_end();
   }
}

static bool si_emit_task_shader(struct si_context *sctx, bool *prefetch)
{
   struct si_compute *program = sctx->ts_shader_state.program;
   struct si_shader *shader = &program->shader;
   const struct ac_shader_config *config = &shader->config;

   if (sctx->ts_shader_state.emitted_program == program)
      return true;

   if (config->scratch_bytes_per_wave) {
      if (!sctx->screen->info.has_scratch_base_registers)
         simple_mtx_lock(&shader->selector->mutex);

      si_set_task_tmpring_size(sctx, config->scratch_bytes_per_wave);

      if (!si_setup_compute_scratch_buffer(sctx->screen, shader,
                                           &sctx->task_scratch_buffer,
                                           sctx->max_seen_task_scratch_bytes_per_wave))
         return false;

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->task_scratch_buffer,
                                RADEON_USAGE_READWRITE | RADEON_PRIO_SCRATCH_BUFFER);
   }

   radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, shader->bo,
                             RADEON_USAGE_READ | RADEON_PRIO_SHADER_BINARY);

   si_emit_task_shader_packets(sctx);

   sctx->ts_shader_state.emitted_program = program;
   *prefetch = true;

   return true;
}

static void si_emit_task_preamble_state(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = sctx->gfx_cs.gang_cs;
   struct si_pm4_state *preamble = sctx->task_preamble_state;

   radeon_begin(cs);
   radeon_emit_array(preamble->base.pm4, preamble->base.ndw);
   radeon_end();
}

static void handle_indirect_resource(struct si_context *sctx, struct si_resource *res)
{
   struct si_screen *sscreen = sctx->screen;

   /* Indirect buffers are read through L2 on GFX9-GFX11, but not other hw. */
   if (sscreen->info.cp_sdma_ge_use_system_memory_scope && res->L2_cache_dirty) {
      sctx->barrier_flags |= SI_BARRIER_WB_L2 | SI_BARRIER_PFP_SYNC_ME;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
      res->L2_cache_dirty = false;
   }

   radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, res,
                             RADEON_USAGE_READ | RADEON_PRIO_DRAW_INDIRECT);
}

static void si_emit_task_wait_packets(struct si_context *sctx)
{
   if (sctx->task_wait_count == sctx->last_task_wait_count)
      return;

   si_cp_write_data(sctx, sctx->task_wait_buf, 0, 4, V_370_MEM, V_370_ME,
                    &sctx->task_wait_count);

   si_cp_wait_mem(sctx, sctx->gfx_cs.gang_cs, sctx->task_wait_buf->gpu_address,
                  sctx->task_wait_count, 0xffffffff, WAIT_REG_MEM_EQUAL);

   sctx->last_task_wait_count = sctx->task_wait_count;
}

static void si_draw_mesh_tasks(struct pipe_context *ctx,
                               const struct pipe_grid_info *info)
{
   struct si_context *sctx = (struct si_context *)ctx;

   /* TODO: TMZ */

   si_check_dirty_buffers_textures(sctx);

   unsigned shader_mask =
      BITFIELD_BIT(MESA_SHADER_TASK) |
      BITFIELD_BIT(MESA_SHADER_MESH) |
      BITFIELD_BIT(MESA_SHADER_FRAGMENT);
   if (sctx->gfx_level < GFX11)
      gfx6_decompress_textures(sctx, shader_mask);
   else if (sctx->gfx_level < GFX12)
      gfx11_decompress_textures(sctx, shader_mask);

   si_need_gfx_cs_space(sctx, 1, 8);

   if (info->indirect)
      handle_indirect_resource(sctx, si_resource(info->indirect));
   if (info->indirect_draw_count)
      handle_indirect_resource(sctx, si_resource(info->indirect_draw_count));

   bool prefetch_task_shader = false;
   if (sctx->ts_shader_state.program) {
      if (!si_init_context_task_shader_states(sctx))
         return;

      bool ret = sctx->ws->cs_check_space(sctx->gfx_cs.gang_cs, 256);
      assert(ret);

      if (!sctx->task_state_init_emitted) {
         si_emit_task_state_init_packet(sctx);
         sctx->task_state_init_emitted = true;

         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->task_ring,
                                   RADEON_USAGE_READWRITE | RADEON_PRIO_SHADER_RINGS);

         si_emit_task_preamble_state(sctx);

         if (sctx->screen->b.caps.mesh.pipeline_statistic_queries)
            si_emit_task_shader_query_state(sctx);
      }

      if (!si_emit_task_shader(sctx, &prefetch_task_shader))
         return;

      si_emit_task_shader_pointers(sctx);
   }

   enum mesa_prim prim = sctx->ms_shader_state.cso->rast_prim;
   si_set_rasterized_prim(sctx, prim, sctx->ms_shader_state.current, true);

   if (sctx->dirty_shaders_mask & SI_MESH_PIPELINE_STATE_DIRTY_MASK)
      si_update_mesh_shader(sctx);

   si_emit_prim_state(sctx);

   uint64_t masked_atoms =
      si_get_atom_bit(sctx, &sctx->atoms.s.gfx_add_all_to_bo_list) |
      si_get_atom_bit(sctx, &sctx->atoms.s.streamout_enable) |
      si_get_atom_bit(sctx, &sctx->atoms.s.ngg_cull_state) |
      si_get_atom_bit(sctx, &sctx->atoms.s.tess_io_layout) |
      si_get_atom_bit(sctx, &sctx->atoms.s.streamout_begin);
   si_emit_all_states(sctx, masked_atoms);

   if (sctx->bo_list_add_all_mesh_resources)
      si_mesh_resources_add_all_to_bo_list(sctx);

   if (sctx->ts_shader_state.program) {
      si_emit_task_wait_packets(sctx);
      si_emit_draw_mesh_tasks_ace_packets(sctx, info, prefetch_task_shader);
      si_emit_draw_mesh_tasks_gfx_packets(sctx, info);
   } else {
      si_emit_draw_mesh_shader_only_packets(sctx, info);
   }

   si_prefetch_mesh_shaders(sctx);

   if (unlikely(sctx->current_saved_cs)) {
      si_trace_emit(sctx);
      si_log_draw_state(sctx, sctx->log);
   }

   sctx->num_draw_calls++;

   /* On Gfx12, this is only used to detect whether a depth texture is in the cleared state. */
   if (sctx->framebuffer.state.zsbuf.texture) {
      struct si_texture *zstex = (struct si_texture *)sctx->framebuffer.state.zsbuf.texture;
      zstex->depth_cleared_level_mask &= ~BITFIELD_BIT(sctx->framebuffer.state.zsbuf.level);
   }
}

void si_init_task_mesh_shader_functions(struct si_context *sctx)
{
   sctx->b.create_ts_state = si_create_ts_state;
   sctx->b.delete_ts_state = si_delete_ts_state;
   sctx->b.bind_ts_state = si_bind_ts_state;
   sctx->b.draw_mesh_tasks = si_draw_mesh_tasks;

   /* mesh shader always run as NGG */
   sctx->ms_shader_state.key.ge.as_ngg = 1;
   /* mesh shader always use ACO */
   sctx->ms_shader_state.key.ge.use_aco = 1;
}
