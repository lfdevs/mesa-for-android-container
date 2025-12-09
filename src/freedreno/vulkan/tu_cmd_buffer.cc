/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_cmd_buffer.h"

#include "vk_common_entrypoints.h"
#include "vk_log.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "tu_buffer.h"
#include "tu_clear_blit.h"
#include "tu_cs.h"
#include "tu_event.h"
#include "tu_image.h"
#include "tu_knl.h"
#include "tu_tracepoints.h"

#include "common/freedreno_gpu_event.h"
#include "common/freedreno_lrz.h"
#include "common/freedreno_vrs.h"

enum tu_cmd_buffer_status {
   TU_CMD_BUFFER_STATUS_IDLE = 0,
   TU_CMD_BUFFER_STATUS_ACTIVE = 1,
};

static struct tu_bo *
tu_cmd_buffer_setup_status_tracking(struct tu_device *device)
{
   struct tu_bo *status_bo;
   VkResult result;

   result = tu_bo_init_new_explicit_iova(
      device, NULL, &status_bo, sizeof(enum tu_cmd_buffer_status), 0,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      TU_BO_ALLOC_INTERNAL_RESOURCE, NULL, "cmd_buffer_status");
   if (result != VK_SUCCESS)
      return NULL;

   result = tu_bo_map(device, status_bo, NULL);
   if (result != VK_SUCCESS)
      return NULL;

   return status_bo;
}

static VkResult
tu_cmd_buffer_status_check_idle(struct tu_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->status_bo == NULL)
      return VK_SUCCESS;

   const enum tu_cmd_buffer_status status =
      *(enum tu_cmd_buffer_status *)cmd_buffer->status_bo->map;

   switch (status) {
   case TU_CMD_BUFFER_STATUS_IDLE:
      return VK_SUCCESS;

   case TU_CMD_BUFFER_STATUS_ACTIVE:
      mesa_loge("Trying to reset or destroy cmd_buffer %p while in use",
                cmd_buffer);
      return vk_errorf(cmd_buffer, VK_ERROR_UNKNOWN,
                       "Trying to reset or destroy while being used");
   default:
      mesa_loge("Something went wrong with cmd_buffer status tracking");
      return vk_error(cmd_buffer, VK_ERROR_UNKNOWN);
   }
}

static inline void
tu_cmd_buffer_status_gpu_write(struct tu_cmd_buffer *cmd_buffer,
                               enum tu_cmd_buffer_status status)
{
   struct tu_cs *cs = &cmd_buffer->cs;

   if (cmd_buffer->status_bo == NULL)
      return;

   static_assert(sizeof(uint32_t) == sizeof(status),
                 "Code below needs adjusting");
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
   tu_cs_emit_qw(cs, cmd_buffer->status_bo->iova);
   tu_cs_emit(cs, (uint32_t)status);
}

static void
tu_clone_trace_range(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                     struct u_trace *dst,
                     struct u_trace_iterator begin, struct u_trace_iterator end)
{
   if (u_trace_iterator_equal(begin, end))
      return;

   tu_cs_emit_wfi(cs);
   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);
   u_trace_clone_append(begin, end, &cmd->trace, cs, tu_copy_buffer);
}

static void
tu_clone_trace(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
               struct u_trace *dst, struct u_trace *src)
{
   tu_clone_trace_range(cmd, cs, dst, u_trace_begin_iterator(src),
         u_trace_end_iterator(src));
}

template <chip CHIP>
void
tu_emit_raw_event_write(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        enum vgt_event_type event,
                        bool needs_seqno)
{
   if (CHIP == A6XX) {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, needs_seqno ? 4 : 1);
      tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(event));
   } else {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, needs_seqno ? 4 : 1);
      tu_cs_emit(cs,
         CP_EVENT_WRITE7_0(.event = event,
                           .write_src = EV_WRITE_USER_32B,
                           .write_dst = EV_DST_RAM,
                           .write_enabled = needs_seqno).value);
   }

   if (needs_seqno) {
      tu_cs_emit_qw(cs, global_iova(cmd, seqno_dummy));
      tu_cs_emit(cs, 0);
   }
}
TU_GENX(tu_emit_raw_event_write);

template <chip CHIP>
void
tu_emit_event_write(struct tu_cmd_buffer *cmd,
                    struct tu_cs *cs,
                    enum fd_gpu_event event)
{
   struct fd_gpu_event_info event_info = fd_gpu_events<CHIP>[event];
   tu_emit_raw_event_write<CHIP>(cmd, cs, event_info.raw_event,
                                 event_info.needs_seqno);
}
TU_GENX(tu_emit_event_write);

/* Emits the tessfactor address to the top-level CS if it hasn't been already.
 * Updating this register requires a WFI if outstanding drawing is using it, but
 * tu6_init_hardware() will have WFIed before we started and no other draws
 * could be using the tessfactor address yet since we only emit one per cmdbuf.
 */
template <chip CHIP>
static void
tu6_lazy_emit_tessfactor_addr(struct tu_cmd_buffer *cmd)
{
   if (cmd->state.tessfactor_addr_set)
      return;

   tu_cs_emit_regs(&cmd->cs, PC_TESS_BASE(CHIP, .qword = cmd->device->tess_bo->iova));
   /* Updating PC_TESS_BASE could race with the next draw which uses it. */
   cmd->state.cache.flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
   cmd->state.tessfactor_addr_set = true;
}

static void
tu6_lazy_init_vsc(struct tu_cmd_buffer *cmd)
{
   struct tu_device *dev = cmd->device;
   uint32_t num_vsc_pipes = dev->physical_device->info->num_vsc_pipes;

   /* VSC buffers:
    * use vsc pitches from the largest values used so far with this device
    * if there hasn't been overflow, there will already be a scratch bo
    * allocated for these sizes
    *
    * if overflow is detected, the stream size is increased by 2x
    */
   mtx_lock(&dev->mutex);

   struct tu6_global *global = dev->global_bo_map;

   uint32_t vsc_draw_overflow = global->vsc_draw_overflow;
   uint32_t vsc_prim_overflow = global->vsc_prim_overflow;

   if (vsc_draw_overflow >= dev->vsc_draw_strm_pitch)
      dev->vsc_draw_strm_pitch = (dev->vsc_draw_strm_pitch - VSC_PAD) * 2 + VSC_PAD;

   if (vsc_prim_overflow >= dev->vsc_prim_strm_pitch)
      dev->vsc_prim_strm_pitch = (dev->vsc_prim_strm_pitch - VSC_PAD) * 2 + VSC_PAD;

   cmd->vsc_prim_strm_pitch = dev->vsc_prim_strm_pitch;
   cmd->vsc_draw_strm_pitch = dev->vsc_draw_strm_pitch;

   mtx_unlock(&dev->mutex);

   uint32_t prim_strm_size = cmd->vsc_prim_strm_pitch * num_vsc_pipes;
   uint32_t draw_strm_size = cmd->vsc_draw_strm_pitch * num_vsc_pipes;
   uint32_t draw_strm_size_size = 4 * num_vsc_pipes;
   uint32_t state_size = 4 * num_vsc_pipes;

   cmd->vsc_size =
      prim_strm_size + draw_strm_size + draw_strm_size_size + state_size;

   cmd->vsc_prim_strm_offset = 0;
   cmd->vsc_draw_strm_offset = prim_strm_size;
   cmd->vsc_draw_strm_size_offset = cmd->vsc_draw_strm_offset + draw_strm_size;
   cmd->vsc_state_offset = cmd->vsc_draw_strm_size_offset + draw_strm_size_size;
}

static void
tu_emit_vis_stream_patchpoint(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                              uint32_t offset)
{
   struct tu_vis_stream_patchpoint patchpoint = {
      .render_pass_idx = cmd->state.tile_render_pass_count,
      .data = cs->cur,
      .iova = tu_cs_get_cur_iova(cs),
      .offset = offset,
   };

   util_dynarray_append(&cmd->vis_stream_patchpoints, patchpoint);
   tu_cs_emit_qw(cs, offset);
}

template <chip CHIP>
static void
tu_emit_vsc(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   if (CHIP == A6XX) {
      tu_cs_emit_pkt4(cs, REG_A6XX_VSC_SIZE_BASE, 2);
      tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_draw_strm_size_offset);
      tu_cs_emit_pkt4(cs, REG_A6XX_VSC_PIPE_DATA_PRIM_BASE, 2);
      tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_prim_strm_offset);
      tu_cs_emit_pkt4(cs, REG_A6XX_VSC_PIPE_DATA_DRAW_BASE, 2);
      tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_draw_strm_offset);
   } else {
      tu_cs_emit_pkt7(cs, CP_SET_PSEUDO_REG, 3 * 3);
      tu_cs_emit(cs, A6XX_CP_SET_PSEUDO_REG__0_PSEUDO_REG(VSC_PIPE_DATA_DRAW_BASE));
      tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_draw_strm_offset);
      tu_cs_emit(cs, A6XX_CP_SET_PSEUDO_REG__0_PSEUDO_REG(VSC_SIZE_BASE));
      tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_draw_strm_size_offset);
      tu_cs_emit(cs, A6XX_CP_SET_PSEUDO_REG__0_PSEUDO_REG(VSC_PIPE_DATA_PRIM_BASE));
      tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_prim_strm_offset);
   }

   cmd->vsc_initialized = true;
}

/* This workaround, copied from the blob, seems to ensure that the BVH node
 * cache is invalidated so that we don't read stale values when multiple BVHs
 * share the same address.
 */
template <chip CHIP>
static void
tu_emit_rt_workaround(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_RT_WA_START);

   tu_cs_emit_regs(cs, SP_CS_HYSTERESIS(CHIP, .dword = 0x10000));
   tu_cs_emit_regs(cs, SP_PS_HYSTERESIS(CHIP, .dword = 0x10000));
   tu_emit_event_write<A7XX>(cmd, cs, FD_DUMMY_EVENT);
   tu_cs_emit_regs(cs, SP_CS_HYSTERESIS(CHIP, .dword = 0));
   tu_cs_emit_regs(cs, SP_PS_HYSTERESIS(CHIP, .dword = 0));
   tu_emit_event_write<A7XX>(cmd, cs, FD_DUMMY_EVENT);
   tu_emit_event_write<A7XX>(cmd, cs, FD_DUMMY_EVENT);
   tu_emit_event_write<A7XX>(cmd, cs, FD_DUMMY_EVENT);
   tu_emit_event_write<A7XX>(cmd, cs, FD_DUMMY_EVENT);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_RT_WA_END);
}

template <chip CHIP>
static void
tu6_emit_flushes(struct tu_cmd_buffer *cmd_buffer,
                 struct tu_cs *cs,
                 struct tu_cache_state *cache)
{
   BITMASK_ENUM(tu_cmd_flush_bits) flushes = cache->flush_bits;
   cache->flush_bits = 0;

   if (TU_DEBUG(FLUSHALL))
      flushes |= TU_CMD_FLAG_ALL_CLEAN | TU_CMD_FLAG_ALL_INVALIDATE;

   if (TU_DEBUG(SYNCDRAW))
      flushes |= TU_CMD_FLAG_WAIT_MEM_WRITES |
                 TU_CMD_FLAG_WAIT_FOR_IDLE |
                 TU_CMD_FLAG_WAIT_FOR_ME;

   /* Experiments show that invalidating CCU while it still has data in it
    * doesn't work, so make sure to always flush before invalidating in case
    * any data remains that hasn't yet been made available through a barrier.
    * However it does seem to work for UCHE.
    */
   if (flushes & (TU_CMD_FLAG_CCU_CLEAN_COLOR |
                  TU_CMD_FLAG_CCU_INVALIDATE_COLOR))
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CCU_CLEAN_COLOR);
   if (flushes & (TU_CMD_FLAG_CCU_CLEAN_DEPTH |
                  TU_CMD_FLAG_CCU_INVALIDATE_DEPTH))
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CCU_CLEAN_DEPTH);
   if (flushes & TU_CMD_FLAG_CCU_INVALIDATE_COLOR)
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CCU_INVALIDATE_COLOR);
   if (flushes & TU_CMD_FLAG_CCU_INVALIDATE_DEPTH)
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CCU_INVALIDATE_DEPTH);
   if (flushes & TU_CMD_FLAG_CACHE_CLEAN)
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CACHE_CLEAN);
   if (flushes & TU_CMD_FLAG_CACHE_INVALIDATE)
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CACHE_INVALIDATE);
   if (flushes & TU_CMD_FLAG_BINDLESS_DESCRIPTOR_INVALIDATE) {
      tu_cs_emit_regs(cs, SP_UPDATE_CNTL(CHIP,
            .cs_bindless = CHIP == A6XX ? 0x1f : 0xff,
            .gfx_bindless = CHIP == A6XX ? 0x1f : 0xff,
      ));
   }
   if (CHIP >= A7XX && flushes & TU_CMD_FLAG_BLIT_CACHE_CLEAN)
      /* On A7XX, blit cache flushes are required to ensure blit writes are visible
       * via UCHE. This isn't necessary on A6XX, all writes should be visible implictly.
       */
      tu_emit_event_write<CHIP>(cmd_buffer, cs, FD_CCU_CLEAN_BLIT_CACHE);
   if (CHIP >= A7XX && (flushes & TU_CMD_FLAG_CCHE_INVALIDATE) &&
       /* Invalidating UCHE seems to also invalidate CCHE */
       !(flushes & TU_CMD_FLAG_CACHE_INVALIDATE))
      tu_cs_emit_pkt7(cs, CP_CCHE_INVALIDATE, 0);
   if (CHIP >= A7XX && (flushes & TU_CMD_FLAG_RTU_INVALIDATE) &&
       cmd_buffer->device->physical_device->info->props.has_rt_workaround)
      tu_emit_rt_workaround<CHIP>(cmd_buffer, cs);
   if (flushes & TU_CMD_FLAG_WAIT_MEM_WRITES)
      tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
   if (flushes & TU_CMD_FLAG_WAIT_FOR_IDLE)
      tu_cs_emit_wfi(cs);
   if (flushes & TU_CMD_FLAG_WAIT_FOR_ME)
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);
}

static void
tu7_write_onchip_val(struct tu_cs *cs, enum tu_onchip_addr addr,
                     uint32_t val)
{
   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
   tu_cs_emit(cs, CP_EVENT_WRITE7_0_WRITE_DST(EV_DST_ONCHIP) |
                  CP_EVENT_WRITE7_0_WRITE_SRC(EV_WRITE_USER_32B) |
                  CP_EVENT_WRITE7_0_EVENT(DUMMY_EVENT) |
                  CP_EVENT_WRITE7_0_WRITE_ENABLED);
   tu_cs_emit_qw(cs, addr);
   tu_cs_emit(cs, val);
}

static void
tu_add_cb_barrier_info(struct tu_cmd_buffer *cmd_buffer)
{
   /* Future concurrent binning cannot happen earlier than the barrier,
    * so we won't need to patch previous patchpoints. Pop them now.
    */
   uint32_t size = util_dynarray_num_elements(&cmd_buffer->cb_control_points,
                                              struct tu_cb_control_point);
   for (int32_t idx = size - 1; idx >= 0; idx--) {
      struct tu_cb_control_point *info = util_dynarray_element(
         &cmd_buffer->cb_control_points, struct tu_cb_control_point, idx);
      if (info->type == TU_CB_CONTROL_TYPE_CB_ENABLED) {
         break;
      }
      (void) util_dynarray_pop(&cmd_buffer->cb_control_points,
                               struct tu_cb_control_point);
   }

   struct tu_cb_control_point barrier_info = {
      .type = TU_CB_CONTROL_TYPE_BARRIER,
   };
   util_dynarray_append(&cmd_buffer->cb_control_points, barrier_info);
}

void
tu7_set_thread_br_patchpoint(struct tu_cmd_buffer *cmd,
                             struct tu_cs *cs,
                             bool force_disable_cb)
{
   tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);

   if (!force_disable_cb) {
      struct tu_cb_control_point info = {
         .type = TU_CB_CONTROL_TYPE_PATCHPOINT,
         .patchpoint = cs->cur,
         .patch_value = CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR),
         .original_value = CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR) |
                           CP_THREAD_CONTROL_0_CONCURRENT_BIN_DISABLE,
      };
      util_dynarray_append(&cmd->cb_control_points, info);
   }

   tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR) |
                     CP_THREAD_CONTROL_0_CONCURRENT_BIN_DISABLE);
}

/* "Normal" cache flushes outside the renderpass, that don't require any special handling */
template <chip CHIP>
void
tu_emit_cache_flush(struct tu_cmd_buffer *cmd_buffer)
{
   struct tu_cs *cs = &cmd_buffer->cs;
   struct tu_cache_state *cache = &cmd_buffer->state.cache;
   BITMASK_ENUM(tu_cmd_flush_bits) flushes = cache->flush_bits;

   tu6_emit_flushes<CHIP>(cmd_buffer, cs, cache);

   if ((flushes & TU_CMD_FLAG_WAIT_FOR_BR) && CHIP >= A7XX &&
       !(cmd_buffer->state.pass && cmd_buffer->state.renderpass_cb_disabled) &&
       !TU_DEBUG(NO_CONCURRENT_BINNING)) {
      trace_start_concurrent_binning_barrier(&cmd_buffer->trace, cs, cmd_buffer);

      /* Wait-for-BR when repeated a lot of times per frame can add up
       * and tank performance.
       */
      struct tu_cs_patchable_state cb_state = tu_cs_patchable_start(cs, 64);

      tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
      tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BOTH));

      tu_cs_emit_pkt7(cs, CP_MODIFY_TIMESTAMP, 1);
      tu_cs_emit(cs, CP_MODIFY_TIMESTAMP_0_ADD(1) |
                     CP_MODIFY_TIMESTAMP_0_OP(MODIFY_TIMESTAMP_ADD_LOCAL));

      tu7_thread_control(cs, CP_SET_THREAD_BV);

      tu7_write_onchip_timestamp(cs, TU_ONCHIP_CB_BV_TIMESTAMP);

      tu7_thread_control(cs, CP_SET_THREAD_BR);

      /* Wait for the previous WAIT_FOR_BR to execute on BV and reset the wait
       * value.
       */
      tu7_wait_onchip_timestamp(cs, TU_ONCHIP_CB_BV_TIMESTAMP);

      /* Signal the wait value. */
      tu7_write_onchip_val(cs, TU_ONCHIP_BARRIER, 1);

      tu7_thread_control(cs, CP_SET_THREAD_BV);

      /* Wait for the value. Note that we must use CP_WAIT_REG_MEM due to a
       * firmware bug which makes CP_WAIT_TIMESTAMP on BV deadlock with
       * preemption when BV waits for BR. Without this bug the whole thing
       * would be much, much simpler.
       */
      tu7_wait_onchip_val(cs, TU_ONCHIP_BARRIER, 1);

      /* Reset the wait value. */
      tu7_write_onchip_val(cs, TU_ONCHIP_BARRIER, 0);

      /* Resetting the wait value happens asynchronously (since it's an
       * EVENT_WRITE), but waiting for it happens synchronously. We need to
       * prevent BV from racing ahead to the next wait before it's reset.
       */
      tu7_wait_onchip_val(cs, TU_ONCHIP_BARRIER, 0);

      tu7_thread_control(cs, CP_SET_THREAD_BR);

      tu_cs_patchable_end(cs, false, &cb_state);

      tu_add_cb_barrier_info(cmd_buffer);

      struct tu_cb_control_point cb_patch = {
         .type = TU_CB_CONTROL_TYPE_PATCHPOINT,
         .patchpoint = cb_state.nop_header,
         .patch_value = cb_state.enable_patch,
         .original_value = cb_state.disable_patch,
      };
      util_dynarray_append(&cmd_buffer->cb_control_points, cb_patch);

      trace_end_concurrent_binning_barrier(&cmd_buffer->trace, cs);
   }
}
TU_GENX(tu_emit_cache_flush);

/* Renderpass cache flushes inside the draw_cs */
template <chip CHIP>
void
tu_emit_cache_flush_renderpass(struct tu_cmd_buffer *cmd_buffer)
{
   if (!cmd_buffer->state.renderpass_cache.flush_bits &&
       likely(!tu_env.debug))
      return;

   struct tu_cs *cs = &cmd_buffer->draw_cs;
   struct tu_cache_state *cache = &cmd_buffer->state.renderpass_cache;

   tu6_emit_flushes<CHIP>(cmd_buffer, cs, cache);
   if (cmd_buffer->state.renderpass_cache.flush_bits &
       TU_CMD_FLAG_BLIT_CACHE_CLEAN) {
      cmd_buffer->state.blit_cache_cleaned = true;
   }
}
TU_GENX(tu_emit_cache_flush_renderpass);

template <chip CHIP>
static void
emit_vpc_attr_buf(struct tu_cs *cs, struct tu_device *dev, bool gmem)
{
   if (!dev->physical_device->info->props.has_gmem_vpc_attr_buf)
      return;

   tu_cs_emit_regs(cs,
      VPC_ATTR_BUF_GMEM_SIZE(CHIP,
               gmem ? dev->physical_device->vpc_attr_buf_size_gmem
                    : dev->physical_device->vpc_attr_buf_size_bypass),
      VPC_ATTR_BUF_GMEM_BASE(CHIP,
               gmem ? dev->physical_device->vpc_attr_buf_offset_gmem
                    : dev->physical_device->vpc_attr_buf_offset_bypass), );
   tu_cs_emit_regs(cs,
      PC_ATTR_BUF_GMEM_SIZE(CHIP,
               gmem ? dev->physical_device->vpc_attr_buf_size_gmem
                    : dev->physical_device->vpc_attr_buf_size_bypass), );
}

template <chip CHIP>
static void
emit_rb_ccu_cntl(struct tu_cs *cs, struct tu_device *dev, bool gmem)
{
   /* The CCUs are a cache that allocates memory from GMEM while facilitating
    * framebuffer caching for sysmem rendering. The CCU is split into two parts,
    * one for color and one for depth. The size and offset of these in GMEM can
    * be configured separately.
    *
    * The most common configuration for the CCU is to occupy as much as possible
    * of GMEM (CACHE_SIZE_FULL) during sysmem rendering as GMEM is unused. On
    * the other hand, when rendering to GMEM, the CCUs can be left enabled at
    * any configuration as they don't interfere with GMEM rendering and only
    * overwrite GMEM when sysmem operations are performed.
    *
    * The vast majority of GMEM rendering doesn't need any sysmem operations
    * but there are some cases where it is required. For example, when the
    * framebuffer isn't aligned to the tile size or with certain MSAA resolves.
    *
    * To correctly handle these cases, we need to be able to switch between
    * sysmem and GMEM rendering. We do this by allocating a carveout at the
    * end of GMEM for the color CCU (as none of these operations are depth)
    * which the color CCU offset is set to and the GMEM size available to the
    * GMEM layout calculations is adjusted accordingly.
    */
   uint32_t color_offset = gmem ? dev->physical_device->ccu_offset_gmem
                                : dev->physical_device->ccu_offset_bypass;

   uint32_t color_offset_hi = color_offset >> 21;
   color_offset &= 0x1fffff;

   uint32_t depth_offset = gmem ? 0
                                : dev->physical_device->ccu_depth_offset_bypass;

   uint32_t depth_offset_hi = depth_offset >> 21;
   depth_offset &= 0x1fffff;

   enum a6xx_ccu_cache_size color_cache_size = !gmem ? CCU_CACHE_SIZE_FULL : !gmem ? CCU_CACHE_SIZE_FULL :
      (a6xx_ccu_cache_size)(dev->physical_device->info->props.gmem_ccu_color_cache_fraction);

   if (CHIP == A7XX) {
      tu_cs_emit_regs(cs, RB_CCU_CACHE_CNTL(CHIP,
         .depth_offset_hi = depth_offset_hi,
         .color_offset_hi = color_offset_hi,
         .depth_cache_size = CCU_CACHE_SIZE_FULL,
         .depth_offset = depth_offset,
         .color_cache_size = color_cache_size,
         .color_offset = color_offset
      ));
   } else {
      tu_cs_emit_regs(cs, RB_CCU_CNTL(CHIP,
         .gmem_fast_clear_disable =
            !dev->physical_device->info->props.has_gmem_fast_clear,
         .concurrent_resolve =
            dev->physical_device->info->props.concurrent_resolve,
         .depth_offset_hi = 0,
         .color_offset_hi = color_offset_hi,
         .depth_cache_size = CCU_CACHE_SIZE_FULL,
         .depth_offset = 0,
         .color_cache_size = color_cache_size,
         .color_offset = color_offset
      ));
   }
}

/* Cache flushes for things that use the color/depth read/write path (i.e.
 * blits and draws). This deals with changing CCU state as well as the usual
 * cache flushing.
 */
template <chip CHIP>
void
tu_emit_cache_flush_ccu(struct tu_cmd_buffer *cmd_buffer,
                        struct tu_cs *cs,
                        enum tu_cmd_ccu_state ccu_state)
{
   assert(ccu_state != TU_CMD_CCU_UNKNOWN);
   /* It's unsafe to flush inside condition because we clear flush_bits */
   assert(!cs->cond_stack_depth);

   /* Changing CCU state must involve invalidating the CCU. In sysmem mode,
    * the CCU may also contain data that we haven't flushed out yet, so we
    * also need to flush. Also, in order to program RB_CCU_CNTL, we need to
    * emit a WFI as it isn't pipelined.
    *
    * Note: On A7XX, with the introduction of RB_CCU_CACHE_CNTL, we no longer need
    * to emit a WFI when changing a subset of CCU state.
    */
   if (ccu_state != cmd_buffer->state.ccu_state) {
      if (cmd_buffer->state.ccu_state != TU_CMD_CCU_GMEM) {
         cmd_buffer->state.cache.flush_bits |=
            TU_CMD_FLAG_CCU_CLEAN_COLOR |
            TU_CMD_FLAG_CCU_CLEAN_DEPTH;
         cmd_buffer->state.cache.pending_flush_bits &= ~(
            TU_CMD_FLAG_CCU_CLEAN_COLOR |
            TU_CMD_FLAG_CCU_CLEAN_DEPTH);
      }
      cmd_buffer->state.cache.flush_bits |=
         TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
         TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
         (CHIP == A6XX ? TU_CMD_FLAG_WAIT_FOR_IDLE : 0);
      cmd_buffer->state.cache.pending_flush_bits &= ~(
         TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
         TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
         (CHIP == A6XX ? TU_CMD_FLAG_WAIT_FOR_IDLE : 0));
   }

   tu_emit_cache_flush<CHIP>(cmd_buffer);

   if (ccu_state != cmd_buffer->state.ccu_state) {
      emit_rb_ccu_cntl<CHIP>(cs, cmd_buffer->device,
                             ccu_state == TU_CMD_CCU_GMEM);
      if (cmd_buffer->device->physical_device->info->props.has_gmem_vpc_attr_buf) {
         tu7_thread_control(cs, CP_SET_THREAD_BOTH);

         emit_vpc_attr_buf<CHIP>(cs, cmd_buffer->device,
                                 ccu_state == TU_CMD_CCU_GMEM);

         tu7_set_thread_br_patchpoint(cmd_buffer, cs, false);
      }
      cmd_buffer->state.ccu_state = ccu_state;
   }
}
TU_GENX(tu_emit_cache_flush_ccu);

template <chip CHIP>
static void
tu6_emit_zs(struct tu_cmd_buffer *cmd,
            const struct tu_subpass *subpass,
            struct tu_cs *cs)
{
   const uint32_t a = subpass->depth_stencil_attachment.attachment;
   if (a == VK_ATTACHMENT_UNUSED) {
      tu_cs_emit_regs(cs,
                      RB_DEPTH_BUFFER_INFO(CHIP, .depth_format = DEPTH6_NONE),
                      A6XX_RB_DEPTH_BUFFER_PITCH(0),
                      A6XX_RB_DEPTH_BUFFER_ARRAY_PITCH(0),
                      A6XX_RB_DEPTH_BUFFER_BASE(0),
                      A6XX_RB_DEPTH_GMEM_BASE(0));

      tu_cs_emit_regs(cs,
                      GRAS_SU_DEPTH_BUFFER_INFO(CHIP, .depth_format = DEPTH6_NONE));

      tu_cs_emit_regs(cs, RB_STENCIL_BUFFER_INFO(CHIP, 0));

      return;
   }

   const struct tu_image_view *iview = cmd->state.attachments[a];
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];
   enum a6xx_depth_format fmt = tu6_pipe2depth(attachment->format);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_BUFFER_INFO, 6);
   tu_cs_emit(cs, RB_DEPTH_BUFFER_INFO(CHIP,
                     .depth_format = fmt,
                     .tilemode = TILE6_3,
                     .losslesscompen = iview->view.ubwc_enabled,
                     ).value);
   if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      tu_cs_image_depth_ref(cs, iview, 0);
   else
      tu_cs_image_ref(cs, &iview->view, 0);
   tu_cs_emit(cs, tu_attachment_gmem_offset(cmd, attachment, 0));

   tu_cs_emit_regs(cs, GRAS_SU_DEPTH_BUFFER_INFO(CHIP, .depth_format = fmt));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_FLAG_BUFFER_BASE, 3);
   tu_cs_image_flag_ref(cs, &iview->view, 0);

   if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
       attachment->format == VK_FORMAT_S8_UINT) {

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCIL_BUFFER_INFO, 6);
      tu_cs_emit(cs, RB_STENCIL_BUFFER_INFO(CHIP,
                        .separate_stencil = true,
                        .tilemode = TILE6_3,
                        ).value);
      if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         tu_cs_image_stencil_ref(cs, iview, 0);
         tu_cs_emit(cs, tu_attachment_gmem_offset_stencil(cmd, attachment, 0));
      } else {
         tu_cs_image_ref(cs, &iview->view, 0);
         tu_cs_emit(cs, tu_attachment_gmem_offset(cmd, attachment, 0));
      }
   } else {
      tu_cs_emit_regs(cs,
                     RB_STENCIL_BUFFER_INFO(CHIP, 0));
   }
}

template <chip CHIP>
static void
tu6_emit_mrt(struct tu_cmd_buffer *cmd,
             const struct tu_subpass *subpass,
             struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   enum a6xx_format mrt0_format = FMT6_NONE;

   uint32_t written = 0;
   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      uint32_t a = subpass->color_attachments[i].attachment;
      unsigned remapped = cmd->vk.dynamic_graphics_state.cal.color_map[i];
      if (a == VK_ATTACHMENT_UNUSED ||
          remapped == MESA_VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = cmd->state.attachments[a];

      tu_cs_emit_regs(cs,
         RB_MRT_BUF_INFO(CHIP, remapped, .dword = iview->view.RB_MRT_BUF_INFO),
         A6XX_RB_MRT_PITCH(remapped, iview->view.pitch),
         A6XX_RB_MRT_ARRAY_PITCH(remapped, iview->view.layer_size),
         A6XX_RB_MRT_BASE(remapped, .qword = tu_layer_address(&iview->view, 0)),
         A6XX_RB_MRT_BASE_GMEM(remapped,
            tu_attachment_gmem_offset(cmd, &cmd->state.pass->attachments[a], 0)
         ),
      );

      tu_cs_emit_regs(cs,
                      A6XX_SP_PS_MRT_REG(remapped, .dword = iview->view.SP_PS_MRT_REG));

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_COLOR_FLAG_BUFFER_ADDR(remapped), 3);
      tu_cs_image_flag_ref(cs, &iview->view, 0);

      if (remapped == 0)
         mrt0_format = (enum a6xx_format) (iview->view.SP_PS_MRT_REG & 0xff);

      written |= 1u << remapped;
   }

   u_foreach_bit (i, ~written) {
      if (i >= MAX_RTS)
         break;

      /* From the VkPipelineRenderingCreateInfo definition:
       *
       *    Valid formats indicate that an attachment can be used - but it
       *    is still valid to set the attachment to NULL when beginning
       *    rendering.
       *
       * This means that with dynamic rendering, pipelines may write to
       * some attachments that are UNUSED here. Setting the format to 0
       * here should prevent them from writing to anything. This also seems
       * to also be required for alpha-to-coverage which can use the alpha
       * value for an otherwise-unused attachment.
       *
       * With VK_EXT_dynamic_rendering_unused_attachments, pipelines may also
       * write to attachments beyond those that exist in the render pass, so
       * we have all attachments not written up to MAX_RTS.
       */
       tu_cs_emit_regs(cs,
         RB_MRT_BUF_INFO(CHIP, i),
         A6XX_RB_MRT_PITCH(i),
         A6XX_RB_MRT_ARRAY_PITCH(i),
         A6XX_RB_MRT_BASE(i),
         A6XX_RB_MRT_BASE_GMEM(i),
       );

       tu_cs_emit_regs(cs,
                       A6XX_SP_PS_MRT_REG(i, .dword = 0));
   }

   tu_cs_emit_regs(cs, GRAS_LRZ_MRT_BUFFER_INFO_0(CHIP, .color_format = mrt0_format));

   const bool dither = subpass->legacy_dithering_enabled;
   const uint32_t dither_cntl =
      A6XX_RB_DITHER_CNTL(
            .dither_mode_mrt0 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt1 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt2 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt3 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt4 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt5 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt6 = dither ? DITHER_ALWAYS : DITHER_DISABLE,
            .dither_mode_mrt7 = dither ? DITHER_ALWAYS : DITHER_DISABLE, )
         .value;
   tu_cs_emit_regs(cs, A6XX_RB_DITHER_CNTL(.dword = dither_cntl));
   if (CHIP >= A7XX) {
      tu_cs_emit_regs(cs, SP_DITHER_CNTL(CHIP, .dword = dither_cntl));
   }

   tu_cs_emit_regs(cs,
                   A6XX_RB_SRGB_CNTL(.dword = subpass->srgb_cntl));
   tu_cs_emit_regs(cs,
                   A6XX_SP_SRGB_CNTL(.dword = subpass->srgb_cntl));

   unsigned layers = MAX2(fb->layers, util_logbase2(subpass->multiview_mask) + 1);
   tu_cs_emit_regs(cs, GRAS_CL_ARRAY_SIZE(CHIP, layers - 1));
}

struct tu_bin_size_params {
   enum a6xx_render_mode render_mode;
   bool force_lrz_write_dis;
   enum a6xx_buffers_location buffers_location;
   enum a6xx_lrz_feedback_mask lrz_feedback_zmode_mask;
   bool force_lrz_dis;
};

template <chip CHIP>
static void
tu6_emit_bin_size(struct tu_cs *cs,
                  uint32_t bin_w,
                  uint32_t bin_h,
                  struct tu_bin_size_params &&p)
{
   if (CHIP == A6XX) {
      tu_cs_emit_regs(
         cs, GRAS_SC_BIN_CNTL(CHIP, .binw = bin_w,
                                   .binh = bin_h,
                                   .render_mode = p.render_mode,
                                   .force_lrz_write_dis = p.force_lrz_write_dis,
                                   .buffers_location = p.buffers_location,
                                   .lrz_feedback_zmode_mask = p.lrz_feedback_zmode_mask, ));
   } else {
      tu_cs_emit_regs(cs,
                      GRAS_SC_BIN_CNTL(CHIP, .binw = bin_w,
                                            .binh = bin_h,
                                            .render_mode = p.render_mode,
                                            .force_lrz_write_dis = p.force_lrz_write_dis,
                                            .lrz_feedback_zmode_mask =
                                               p.lrz_feedback_zmode_mask,
                                            .force_lrz_dis = p.force_lrz_dis));
   }

   tu_cs_emit_regs(cs, RB_CNTL(CHIP,
                        .binw = bin_w,
                        .binh = bin_h,
                        .render_mode = p.render_mode,
                        .force_lrz_write_dis = p.force_lrz_write_dis,
                        .buffers_location = p.buffers_location,
                        .lrz_feedback_zmode_mask = p.lrz_feedback_zmode_mask, ));

   /* no flag for RB_RESOLVE_CNTL_3... */
   tu_cs_emit_regs(cs, RB_RESOLVE_CNTL_3(CHIP, .binw = bin_w, .binh = bin_h));
}

template <chip CHIP>
static void
tu6_emit_render_cntl(struct tu_cmd_buffer *cmd,
                     const struct tu_subpass *subpass,
                     struct tu_cs *cs,
                     bool binning);

template <>
void
tu6_emit_render_cntl<A6XX>(struct tu_cmd_buffer *cmd,
                     const struct tu_subpass *subpass,
                     struct tu_cs *cs,
                     bool binning)
{
   /* doesn't RB_RENDER_CNTL set differently for binning pass: */
   bool no_track = !cmd->device->physical_device->info->props.has_cp_reg_write;
   uint32_t cntl = 0;
   cntl |= A6XX_RB_RENDER_CNTL_CCUSINGLECACHELINESIZE(2);
   if (binning) {
      if (no_track)
         return;
      cntl |= A6XX_RB_RENDER_CNTL_FS_DISABLE;
   } else {
      uint32_t mrts_ubwc_enable = 0;
      for (uint32_t i = 0; i < subpass->color_count; ++i) {
         uint32_t a = subpass->color_attachments[i].attachment;
         unsigned remapped = cmd->vk.dynamic_graphics_state.cal.color_map[i];
         if (a == VK_ATTACHMENT_UNUSED ||
             remapped == MESA_VK_ATTACHMENT_UNUSED)
            continue;

         const struct tu_image_view *iview = cmd->state.attachments[a];
         if (iview->view.ubwc_enabled)
            mrts_ubwc_enable |= 1 << remapped;
      }

      cntl |= A6XX_RB_RENDER_CNTL_FLAG_MRTS(mrts_ubwc_enable);

      const uint32_t a = subpass->depth_stencil_attachment.attachment;
      if (a != VK_ATTACHMENT_UNUSED) {
         const struct tu_image_view *iview = cmd->state.attachments[a];
         if (iview->view.ubwc_enabled)
            cntl |= A6XX_RB_RENDER_CNTL_FLAG_DEPTH;
      }

      if (no_track) {
         tu_cs_emit_pkt4(cs, REG_A6XX_RB_RENDER_CNTL, 1);
         tu_cs_emit(cs, cntl);
         return;
      }

      /* In the !binning case, we need to set RB_RENDER_CNTL in the draw_cs
       * in order to set it correctly for the different subpasses. However,
       * that means the packets we're emitting also happen during binning. So
       * we need to guard the write on !BINNING at CP execution time.
       */
      tu_cs_reserve(cs, 3 + 4);
      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                     CP_COND_REG_EXEC_0_GMEM | CP_COND_REG_EXEC_0_SYSMEM);
      tu_cs_emit(cs, RENDER_MODE_CP_COND_REG_EXEC_1_DWORDS(4));
   }

   tu_cs_emit_pkt7(cs, CP_REG_WRITE, 3);
   tu_cs_emit(cs, CP_REG_WRITE_0_TRACKER(TRACK_RENDER_CNTL));
   tu_cs_emit(cs, REG_A6XX_RB_RENDER_CNTL);
   tu_cs_emit(cs, cntl);
}

template <>
void
tu6_emit_render_cntl<A7XX>(struct tu_cmd_buffer *cmd,
                     const struct tu_subpass *subpass,
                     struct tu_cs *cs,
                     bool binning)
{
}

template <>
void
tu6_emit_render_cntl<A8XX>(struct tu_cmd_buffer *cmd,
                     const struct tu_subpass *subpass,
                     struct tu_cs *cs,
                     bool binning)
{
}

static void
tu6_emit_blit_scissor(struct tu_cmd_buffer *cmd, struct tu_cs *cs, bool align,
                      bool used_by_sysmem)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const VkRect2D *render_area = &cmd->state.render_area;

   /* Avoid assertion fails with an empty render area at (0, 0) where the
    * subtraction below wraps around. Empty render areas should be forced to
    * the sysmem path by use_sysmem_rendering(). It's not even clear whether
    * an empty scissor here works, and the blob seems to force sysmem too as
    * it sets something wrong (non-empty) for the scissor.
    */
   if (render_area->extent.width == 0 ||
       render_area->extent.height == 0)
      return;

   uint32_t x1 = render_area->offset.x;
   uint32_t y1 = render_area->offset.y;
   uint32_t x2 = x1 + render_area->extent.width - 1;
   uint32_t y2 = y1 + render_area->extent.height - 1;

   if (align) {
      x1 = x1 & ~(phys_dev->info->gmem_align_w - 1);
      y1 = y1 & ~(phys_dev->info->gmem_align_h - 1);
      x2 = ALIGN_POT(x2 + 1, phys_dev->info->gmem_align_w) - 1;
      y2 = ALIGN_POT(y2 + 1, phys_dev->info->gmem_align_h) - 1;
   }

   /* With FDM offset, bins are shifted to the right in GMEM space compared to
    * framebuffer space. We do not use RB_BLIT_SCISSOR_* for loads and stores
    * because those do not use the fast path, but we do use it for
    * LOAD_OP_CLEAR. Expand the render area so that GMEM clears work
    * correctly. We may over-clear but that's ok because the store is clipped
    * to the render area.
    */
   if (tu_enable_fdm_offset(cmd)) {
      const struct tu_tiling_config *tiling = cmd->state.tiling;

      /* If this is a generic clear that's also used in sysmem mode then we
       * need to emit the unmodified render area in sysmem mode because
       * over-clearing is not allowed.
       */
      if (used_by_sysmem) {
         tu_cs_emit_regs(cs,
                         A6XX_RB_RESOLVE_CNTL_1(.x = x1, .y = y1),
                         A6XX_RB_RESOLVE_CNTL_2(.x = x2, .y = y2));
         tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                                CP_COND_REG_EXEC_0_GMEM);
      }

      x2 += tiling->tile0.width;
      y2 += tiling->tile0.height;
      tu_cs_emit_regs(cs,
                      A6XX_RB_RESOLVE_CNTL_1(.x = x1, .y = y1),
                      A6XX_RB_RESOLVE_CNTL_2(.x = x2, .y = y2));

      if (used_by_sysmem) {
         tu_cond_exec_end(cs);
      }
   } else {
      tu_cs_emit_regs(cs,
                      A6XX_RB_RESOLVE_CNTL_1(.x = x1, .y = y1),
                      A6XX_RB_RESOLVE_CNTL_2(.x = x2, .y = y2));
   }
}

template <chip CHIP>
void
tu6_emit_window_scissor(struct tu_cs *cs,
                        uint32_t x1,
                        uint32_t y1,
                        uint32_t x2,
                        uint32_t y2)
{
   tu_cs_emit_regs(cs,
                   GRAS_SC_WINDOW_SCISSOR_TL(CHIP, .x = x1, .y = y1),
                   GRAS_SC_WINDOW_SCISSOR_BR(CHIP, .x = x2, .y = y2));

   tu_cs_emit_regs(cs,
                   GRAS_A2D_SCISSOR_TL(CHIP, .x = x1, .y = y1),
                   GRAS_A2D_SCISSOR_BR(CHIP, .x = x2, .y = y2));
}
TU_GENX(tu6_emit_window_scissor);

template <chip CHIP>
void
tu6_emit_window_offset(struct tu_cs *cs, uint32_t x1, uint32_t y1)
{
   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET(.x = x1, .y = y1));

   tu_cs_emit_regs(cs,
                   A6XX_RB_RESOLVE_WINDOW_OFFSET(.x = x1, .y = y1));

   tu_cs_emit_regs(cs,
                   SP_WINDOW_OFFSET(CHIP, .x = x1, .y = y1));

   tu_cs_emit_regs(cs,
                   A6XX_TPL1_WINDOW_OFFSET(.x = x1, .y = y1));

   if (CHIP >= A7XX) {
      tu_cs_emit_regs(cs,
                     TPL1_A2D_WINDOW_OFFSET(CHIP, .x = x1, .y = y1));
   }
}

void
tu6_apply_depth_bounds_workaround(struct tu_device *device,
                                  uint32_t *rb_depth_cntl)
{
   if (!device->physical_device->info->props.depth_bounds_require_depth_test_quirk)
      return;

   /* On some GPUs it is necessary to enable z test for depth bounds test when
    * UBWC is enabled. Otherwise, the GPU would hang. FUNC_ALWAYS is required to
    * pass z test. Relevant tests:
    *  dEQP-VK.pipeline.extended_dynamic_state.two_draws_dynamic.depth_bounds_test_disable
    *  dEQP-VK.dynamic_state.ds_state.depth_bounds_1
    */
   *rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE |
                     A6XX_RB_DEPTH_CNTL_ZFUNC(FUNC_ALWAYS);
}

static void
tu_cs_emit_draw_state(struct tu_cs *cs, uint32_t id, struct tu_draw_state state)
{
   uint32_t enable_mask;
   switch (id) {
   case TU_DRAW_STATE_VS:
   case TU_DRAW_STATE_FS:
   case TU_DRAW_STATE_VPC:
   /* The blob seems to not enable this (DESC_SETS_LOAD) for binning, even
    * when resources would actually be used in the binning shader.
    * Presumably the overhead of prefetching the resources isn't
    * worth it.
    */
   case TU_DRAW_STATE_DESC_SETS_LOAD:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM |
                    CP_SET_DRAW_STATE__0_SYSMEM;
      break;
   case TU_DRAW_STATE_VS_BINNING:
   case TU_DRAW_STATE_GS_BINNING:
      enable_mask = CP_SET_DRAW_STATE__0_BINNING;
      break;
   case TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM;
      break;
   case TU_DRAW_STATE_PRIM_MODE_GMEM:
      /* On a7xx the prim mode is the same for gmem and sysmem, and it no
       * longer depends on dynamic state, so we reuse the gmem state for
       * everything:
       */
      if (cs->device->physical_device->info->props.has_coherent_ubwc_flag_caches) {
         enable_mask = CP_SET_DRAW_STATE__0_GMEM |
                       CP_SET_DRAW_STATE__0_SYSMEM |
                       CP_SET_DRAW_STATE__0_BINNING;
      } else {
         enable_mask = CP_SET_DRAW_STATE__0_GMEM;
      }
      break;
   case TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM:
      enable_mask = CP_SET_DRAW_STATE__0_SYSMEM;
      break;
   case TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_PRIM_MODE_SYSMEM:
      if (!cs->device->physical_device->info->props.has_coherent_ubwc_flag_caches) {
         /* By also applying the state during binning we ensure that there
         * is no rotation applied, by previous A6XX_GRAS_SC_CNTL::rotation.
         */
         enable_mask =
            CP_SET_DRAW_STATE__0_SYSMEM | CP_SET_DRAW_STATE__0_BINNING;
      } else {
         static_assert(TU_DYNAMIC_STATE_PRIM_MODE_SYSMEM ==
                       TU_DYNAMIC_STATE_A7XX_FRAGMENT_SHADING_RATE);
         enable_mask = CP_SET_DRAW_STATE__0_GMEM |
                       CP_SET_DRAW_STATE__0_SYSMEM |
                       CP_SET_DRAW_STATE__0_BINNING;
      }

      break;
   default:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM |
                    CP_SET_DRAW_STATE__0_SYSMEM |
                    CP_SET_DRAW_STATE__0_BINNING;
      break;
   }

   STATIC_ASSERT(TU_DRAW_STATE_COUNT <= 32);

   /* We need to reload the descriptors every time the descriptor sets
    * change. However, the commands we send only depend on the pipeline
    * because the whole point is to cache descriptors which are used by the
    * pipeline. There's a problem here, in that the firmware has an
    * "optimization" which skips executing groups that are set to the same
    * value as the last draw. This means that if the descriptor sets change
    * but not the pipeline, we'd try to re-execute the same buffer which
    * the firmware would ignore and we wouldn't pre-load the new
    * descriptors. Set the DIRTY bit to avoid this optimization.
    *
    * We set the dirty bit for shader draw states because they contain
    * CP_LOAD_STATE packets that are invalidated by the PROGRAM_CONFIG draw
    * state, so if PROGRAM_CONFIG changes but one of the shaders stays the
    * same then we still need to re-emit everything. The GLES blob which
    * implements separate shader draw states does the same thing.
    *
    * We also need to set this bit for draw states which may be patched by the
    * GPU, because their underlying memory may change between setting the draw
    * state.
    */
   if (id == TU_DRAW_STATE_DESC_SETS_LOAD ||
       id == TU_DRAW_STATE_VS ||
       id == TU_DRAW_STATE_VS_BINNING ||
       id == TU_DRAW_STATE_HS ||
       id == TU_DRAW_STATE_DS ||
       id == TU_DRAW_STATE_GS ||
       id == TU_DRAW_STATE_GS_BINNING ||
       id == TU_DRAW_STATE_FS ||
       state.writeable)
      enable_mask |= CP_SET_DRAW_STATE__0_DIRTY;

   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(state.size) |
                  enable_mask |
                  CP_SET_DRAW_STATE__0_GROUP_ID(id) |
                  COND(!state.size || !state.iova, CP_SET_DRAW_STATE__0_DISABLE));
   tu_cs_emit_qw(cs, state.iova);
}

template <chip CHIP>
void
tu6_emit_msaa(struct tu_cs *cs, VkSampleCountFlagBits vk_samples,
              bool msaa_disable)
{
   const enum a3xx_msaa_samples samples = tu_msaa_samples(vk_samples);
   msaa_disable |= (samples == MSAA_ONE);
   tu_cs_emit_regs(cs,
                   A6XX_TPL1_RAS_MSAA_CNTL(samples),
                   A6XX_TPL1_DEST_MSAA_CNTL(.samples = samples,
                                             .msaa_disable = msaa_disable));

   tu_cs_emit_regs(cs,
                   GRAS_SC_RAS_MSAA_CNTL(CHIP, samples),
                   GRAS_SC_DEST_MSAA_CNTL(CHIP, .samples = samples,
                                               .msaa_disable = msaa_disable));

   tu_cs_emit_regs(cs,
                   A6XX_RB_RAS_MSAA_CNTL(samples),
                   A6XX_RB_DEST_MSAA_CNTL(.samples = samples,
                                          .msaa_disable = msaa_disable));
}
TU_GENX(tu6_emit_msaa);

template <chip CHIP>
static void
tu6_update_msaa(struct tu_cmd_buffer *cmd)
{
   VkSampleCountFlagBits samples =
      cmd->vk.dynamic_graphics_state.ms.rasterization_samples;;

   /* The samples may not be set by the pipeline or dynamically if raster
    * discard is enabled. We can set any valid value, but don't set the
    * default invalid value of 0.
    */
   if (samples == 0)
      samples = VK_SAMPLE_COUNT_1_BIT;
   tu6_emit_msaa<CHIP>(&cmd->draw_cs, samples, cmd->state.msaa_disable);
}

template <chip CHIP>
static void
tu6_update_msaa_disable(struct tu_cmd_buffer *cmd)
{
   VkPrimitiveTopology topology = 
      (VkPrimitiveTopology)cmd->vk.dynamic_graphics_state.ia.primitive_topology;
   bool is_line =
      topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
      topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY ||
      topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
      topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY ||
      (topology == VK_PRIMITIVE_TOPOLOGY_PATCH_LIST &&
       cmd->state.shaders[MESA_SHADER_TESS_EVAL] &&
       cmd->state.shaders[MESA_SHADER_TESS_EVAL]->variant &&
       cmd->state.shaders[MESA_SHADER_TESS_EVAL]->variant->key.tessellation == IR3_TESS_ISOLINES);
   bool msaa_disable = is_line &&
      cmd->vk.dynamic_graphics_state.rs.line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_KHR;

   if (cmd->state.msaa_disable != msaa_disable) {
      cmd->state.msaa_disable = msaa_disable;
      tu6_update_msaa<CHIP>(cmd);
   }
}

static const struct tu_vsc_config *
tu_vsc_config(struct tu_cmd_buffer *cmd, const struct tu_tiling_config *tiling)
{
   if (tu_enable_fdm_offset(cmd))
      return &tiling->fdm_offset_vsc;
   return &tiling->vsc;
}

static bool
use_hw_binning(struct tu_cmd_buffer *cmd)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_tiling_config *tiling = &fb->tiling[cmd->state.gmem_layout];
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, tiling);

   /* XFB commands are emitted for BINNING || SYSMEM, which makes it
    * incompatible with non-hw binning GMEM rendering. this is required because
    * some of the XFB commands need to only be executed once.
    * use_sysmem_rendering() should have made sure we only ended up here if no
    * XFB was used.
    */
   if (cmd->state.rp.xfb_used) {
      assert(vsc->binning_possible);
      return true;
   }

   /* VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT emulates GL_PRIMITIVES_GENERATED,
    * which wasn't designed to care about tilers and expects the result not to
    * be multiplied by tile count.
    * See https://gitlab.khronos.org/vulkan/vulkan/-/issues/3131
    */
   if (cmd->state.rp.has_prim_generated_query_in_rp ||
       cmd->state.prim_generated_query_running_before_rp) {
      assert(vsc->binning_possible);
      return true;
   }

   return vsc->binning;
}

static bool
use_sysmem_rendering(struct tu_cmd_buffer *cmd,
                     struct tu_renderpass_result **autotune_result)
{
   if (TU_DEBUG(SYSMEM)) {
      cmd->state.rp.gmem_disable_reason = "TU_DEBUG(SYSMEM)";
      return true;
   }

   /* can't fit attachments into gmem */
   if (!cmd->state.tiling->possible) {
      cmd->state.rp.gmem_disable_reason = "Can't fit attachments into gmem";
      return true;
   }

   /* Use sysmem for empty render areas */
   if (cmd->state.render_area.extent.width == 0 ||
       cmd->state.render_area.extent.height == 0) {
      cmd->state.rp.gmem_disable_reason = "Render area is empty";
      return true;
   }

   if (cmd->state.rp.has_tess) {
      cmd->state.rp.gmem_disable_reason = "Uses tessellation shaders";
      return true;
   }

   if (cmd->state.rp.disable_gmem) {
      /* gmem_disable_reason is set where disable_gmem is set. */
      return true;
   }

   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, cmd->state.tiling);

   /* XFB is incompatible with non-hw binning GMEM rendering, see use_hw_binning */
   if (cmd->state.rp.xfb_used && !vsc->binning_possible) {
      cmd->state.rp.gmem_disable_reason =
         "XFB is incompatible with non-hw binning GMEM rendering";
      return true;
   }

   /* QUERY_TYPE_PRIMITIVES_GENERATED is incompatible with non-hw binning
    * GMEM rendering, see use_hw_binning.
    */
   if ((cmd->state.rp.has_prim_generated_query_in_rp ||
        cmd->state.prim_generated_query_running_before_rp) &&
       !vsc->binning_possible) {
      cmd->state.rp.gmem_disable_reason =
         "QUERY_TYPE_PRIMITIVES_GENERATED is incompatible with non-hw binning GMEM rendering";
      return true;
   }

   if (TU_DEBUG(GMEM))
      return false;

   bool use_sysmem = tu_autotune_use_bypass(&cmd->device->autotune,
                                            cmd, autotune_result);
   if (*autotune_result) {
      list_addtail(&(*autotune_result)->node, &cmd->renderpass_autotune_results);
   }

   if (use_sysmem) {
      cmd->state.rp.gmem_disable_reason = "Autotune selected sysmem";
   }

   return use_sysmem;
}

/* Optimization: there is no reason to load gmem if there is no
 * geometry to process. COND_REG_EXEC predicate is set here,
 * but the actual skip happens in tu_load_gmem_attachment() and tile_store_cs,
 * for each blit separately.
 */
template <chip CHIP>
static void
tu6_emit_cond_for_load_stores(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                              uint32_t pipe, uint32_t slot, bool skip_wfm)
{
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, cmd->state.tiling);

   if (vsc->binning_possible &&
       cmd->state.pass->has_cond_load_store) {
      if (CHIP >= A7XX) {
         tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
         tu_cs_emit(cs, A6XX_CP_REG_TEST_0_SCRATCH_MEM_OFFSET(pipe) |
                        A6XX_CP_REG_TEST_0_SOURCE(SOURCE_SCRATCH_MEM) |
                        A6XX_CP_REG_TEST_0_BIT(slot) |
                        A6XX_CP_REG_TEST_0_SKIP_WAIT_FOR_ME);
      } else {
         tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
         tu_cs_emit(cs, A6XX_CP_REG_TEST_0_REG(REG_A6XX_VSC_CHANNEL_VISIBILITY(pipe)) |
                        A6XX_CP_REG_TEST_0_BIT(slot) |
                        COND(skip_wfm, A6XX_CP_REG_TEST_0_SKIP_WAIT_FOR_ME));
      }
   } else {
      /* COND_REG_EXECs are not emitted in non-binning case */
   }
}

struct tu_tile_config {
   VkOffset2D pos;
   uint32_t pipe;
   uint32_t slot_mask;
   VkExtent2D extent;
   VkExtent2D frag_areas[MAX_VIEWS];
};

/* For bin offsetting we want to do "Euclidean division," where the remainder
 * (i.e. the offset of the bin) is always positive. Unfortunately C/C++
 * remainder and division don't do this, so we have to implement it ourselves.
 *
 * For example, we should have:
 *
 * euclid_rem(-3, 4) = 1
 * euclid_rem(-4, 4) = 0
 * euclid_rem(-4, 4) = 3
 */

static int32_t
euclid_rem(int32_t divisor, int32_t divisend)
{
   if (divisor >= 0)
      return divisor % divisend;
   int32_t tmp = divisend - (-divisor % divisend);
   return tmp == divisend ? 0 : tmp;
}

/* Calculate how much the bins for a given view should be shifted to the left
 * and upwards, given the application-provided FDM offset.
 */
static VkOffset2D
tu_bin_offset(VkOffset2D fdm_offset, const struct tu_tiling_config *tiling)
{
   return (VkOffset2D) {
      euclid_rem(-fdm_offset.x, tiling->tile0.width),
      euclid_rem(-fdm_offset.y, tiling->tile0.height),
   };
}

static uint32_t
tu_fdm_num_layers(const struct tu_cmd_buffer *cmd)
{
   return cmd->state.pass->num_views ? cmd->state.pass->num_views : 
      (cmd->state.fdm_per_layer ? cmd->state.framebuffer->layers : 1);
}

template <chip CHIP>
static void
tu6_emit_bin_size_gmem(struct tu_cmd_buffer *cmd,
                       struct tu_cs *cs,
                       enum a6xx_buffers_location buffers_location,
                       bool disable_lrz)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   bool hw_binning = use_hw_binning(cmd);

   tu6_emit_bin_size<CHIP>(
      cs, buffers_location == BUFFERS_IN_GMEM ? tiling->tile0.width : 0,
      buffers_location == BUFFERS_IN_GMEM ? tiling->tile0.height : 0,
      {
         .render_mode = RENDERING_PASS,
         .force_lrz_write_dis = !phys_dev->info->props.has_lrz_feedback,
         .buffers_location = buffers_location,
         .lrz_feedback_zmode_mask =
            phys_dev->info->props.has_lrz_feedback
               ? (hw_binning ? LRZ_FEEDBACK_EARLY_Z_OR_EARLY_Z_LATE_Z :
                  LRZ_FEEDBACK_EARLY_Z_LATE_Z)
               : LRZ_FEEDBACK_NONE,
         .force_lrz_dis = CHIP >= A7XX && disable_lrz,
      });

}

/* Set always-identical registers used specifically for GMEM */
template <chip CHIP>
static void
tu7_emit_tile_render_begin_regs(struct tu_cs *cs)
{
   tu_cs_emit_regs(cs, RB_BUFFER_CNTL(CHIP, 0x0));
   tu_cs_emit_regs(cs, RB_CLEAR_TARGET(CHIP, .clear_mode = CLEAR_MODE_GMEM));
}

/* Set always-identical registers used specifically for sysmem */
template <chip CHIP>
static void
tu7_emit_sysmem_render_begin_regs(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_regs(cs, RB_BUFFER_CNTL(CHIP,
      .z_sysmem = true,
      .s_sysmem = true,
      .rt0_sysmem = true,
      .rt1_sysmem = true,
      .rt2_sysmem = true,
      .rt3_sysmem = true,
      .rt4_sysmem = true,
      .rt5_sysmem = true,
      .rt6_sysmem = true,
      .rt7_sysmem = true,
   ));

   tu_cs_emit_regs(cs, RB_CLEAR_TARGET(CHIP, .clear_mode = CLEAR_MODE_SYSMEM));
}

template <chip CHIP>
static void
tu6_emit_tile_select(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_tile_config *tile,
                     bool fdm, const VkOffset2D *fdm_offsets)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, tiling);
   bool hw_binning = use_hw_binning(cmd);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BIN_RENDER_START) |
                  A6XX_CP_SET_MARKER_0_USES_GMEM);

   if (CHIP == A6XX && cmd->device->physical_device->has_preemption) {
      if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
         tu_cs_set_writeable(cs, true);

      tu_emit_vsc<CHIP>(cmd, &cmd->cs);

      if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
         tu_cs_set_writeable(cs, false);
   }

   unsigned views = tu_fdm_num_layers(cmd);
   unsigned layers = MAX2(cmd->state.pass->num_views,
                          cmd->state.framebuffer->layers);
   bool bin_is_scaled = false;

   if (fdm) {
      for (unsigned i = 0; i < views; i++) {
         if (tile->frag_areas[i].width != 1 ||
             tile->frag_areas[i].height != 1) {
            bin_is_scaled = true;
            break;
         }
      }
   }

   bool bin_scale_en =
      cmd->device->physical_device->info->props.has_hw_bin_scaling &&
      layers <= MAX_HW_SCALED_VIEWS && !cmd->state.rp.shared_viewport &&
      bin_is_scaled;

   /* We cannot support LRZ if we cannot use HW bin scaling and the bin is
    * scaled (i.e. less than full resolution)
    */
   bool disable_lrz = bin_is_scaled && !bin_scale_en;

   /* We cannot support LRZ for the first row and column because the offset
    * required wouldn't be aligned to HW requirements.
    */
   if (fdm_offsets && (tile->pos.x == 0 || tile->pos.y == 0))
      disable_lrz = true;

   /* When using custom resolve we need to re-emit these regs as they are
    * overwritten when switching to sysmem.
    */
   if (CHIP >= A7XX &&
       cmd->state.pass->subpasses[cmd->state.pass->subpass_count - 1].custom_resolve) {
      tu7_emit_tile_render_begin_regs<CHIP>(cs);
   }

   tu6_emit_bin_size_gmem<CHIP>(cmd, cs, BUFFERS_IN_GMEM, disable_lrz);

   tu_cs_emit_regs(cs,
                   A6XX_VFD_RENDER_MODE(RENDERING_PASS));

   const uint32_t x1 = tiling->tile0.width * tile->pos.x;
   const uint32_t y1 = tiling->tile0.height * tile->pos.y;

   const uint32_t x2 = MIN2(x1 + tiling->tile0.width, MAX_VIEWPORT_SIZE);
   const uint32_t y2 = MIN2(y1 + tiling->tile0.height, MAX_VIEWPORT_SIZE);

   if (bin_scale_en) {
      /* It seems that the window scissor happens *before*
       * GRAS_BIN_FOVEAT_OFFSET_* is applied to the fragment coordinates,
       * unlike the window offset which happens after it is applied. This
       * means that the window scissor cannot do its job and we have to
       * disable it by setting it to the entire FB size (plus an extra tile
       * size, in case GRAS_BIN_FOVEAT_OFFSET_* is not in use). With FDM it is
       * effectively replaced by the user's scissor anyway.
       */
      uint32_t width = fb->width + tiling->tile0.width;
      uint32_t height = fb->height + tiling->tile0.height;
      tu6_emit_window_scissor<CHIP>(cs, 0, 0, width, height);
   } else {
      tu6_emit_window_scissor<CHIP>(cs, x1, y1, x2 - 1, y2 - 1);
   }
   tu6_emit_window_offset<CHIP>(cs, x1, y1);

   unsigned slot = ffs(tile->slot_mask) - 1;

   if (hw_binning) {
      bool abs_mask =
         cmd->device->physical_device->info->props.has_abs_bin_mask;
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_SET_BIN_DATA5_OFFSET, abs_mask ? 5 : 4);
      /* A702 also sets BIT(0) but that hangchecks */
      tu_cs_emit(cs, vsc->pipe_sizes[tile->pipe] |
                     CP_SET_BIN_DATA5_0_VSC_N(slot) |
                     CP_SET_BIN_DATA5_0_VSC_MASK(tile->slot_mask >> slot) |
                     COND(abs_mask, CP_SET_BIN_DATA5_0_ABS_MASK(ABS_MASK)));
      if (abs_mask)
         tu_cs_emit(cs, tile->slot_mask);
      tu_cs_emit(cs, tile->pipe * cmd->vsc_draw_strm_pitch);
      tu_cs_emit(cs, tile->pipe * 4);
      tu_cs_emit(cs, tile->pipe * cmd->vsc_prim_strm_pitch);
   }

   if (util_is_power_of_two_nonzero(tile->slot_mask))
      tu6_emit_cond_for_load_stores<CHIP>(cmd, cs, tile->pipe, slot, hw_binning);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, !hw_binning);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);

   if (fdm) {
      VkRect2D bin = {
         { x1, y1 },
         { (x2 - x1) * tile->extent.width, (y2 - y1) * tile->extent.height }
      };
      VkRect2D bins[views];
      VkOffset2D frag_offsets[MAX_VIEWS];
      for (unsigned i = 0; i < views; i++) {
         frag_offsets[i] = (VkOffset2D) { 0, 0 };

         if (!fdm_offsets || cmd->state.rp.shared_viewport) {
            bins[i] = bin;
            continue;
         }

         VkOffset2D bin_offset = tu_bin_offset(fdm_offsets[i], tiling);

         bins[i].offset.x = MAX2(0, (int32_t)x1 - bin_offset.x);
         bins[i].offset.y = MAX2(0, (int32_t)y1 - bin_offset.y);
         bins[i].extent.width =
            MAX2(MIN2((int32_t)x1 + bin.extent.width - bin_offset.x, MAX_VIEWPORT_SIZE) - bins[i].offset.x, 0);
         bins[i].extent.height =
            MAX2(MIN2((int32_t)y1 + bin.extent.height - bin_offset.y, MAX_VIEWPORT_SIZE) - bins[i].offset.y, 0);
      }

      if (cmd->device->physical_device->info->props.has_hw_bin_scaling) {
         if (bin_scale_en) {
            VkExtent2D frag_areas[MAX_HW_SCALED_VIEWS];
            for (unsigned i = 0; i < MAX_HW_SCALED_VIEWS; i++) {
               if (i >= layers) {
                  /* Make sure unused views aren't garbage */
                  frag_areas[i] = (VkExtent2D) {1, 1};
                  frag_offsets[i] = (VkOffset2D) { 0, 0 };
                  continue;
               }

               /* The HW bin offset is always per-layer, whereas if there is
                * more than 1 layer (i.e. layered rendering instead of
                * multiview rendering) and FDM is not per-layer then all
                * layers implicitly use the scale from FDM layer 0. We have to
                * explicitly broadcast it here.
                */
               unsigned view = MIN2(i, views - 1);
               frag_areas[i] = tile->frag_areas[view];
               frag_offsets[i].x = x1 - x1 / tile->frag_areas[view].width;
               frag_offsets[i].y = y1 - y1 / tile->frag_areas[view].height;
            }

            tu_cs_emit_regs(cs, GRAS_BIN_FOVEAT(CHIP,
                  .binscaleen = bin_scale_en,
                  .xscale_0 = (enum a7xx_bin_scale)util_logbase2(frag_areas[0].width),
                  .yscale_0 = (enum a7xx_bin_scale)util_logbase2(frag_areas[0].height),
                  .xscale_1 = (enum a7xx_bin_scale)util_logbase2(frag_areas[1].width),
                  .yscale_1 = (enum a7xx_bin_scale)util_logbase2(frag_areas[1].height),
                  .xscale_2 = (enum a7xx_bin_scale)util_logbase2(frag_areas[2].width),
                  .yscale_2 = (enum a7xx_bin_scale)util_logbase2(frag_areas[2].height),
                  .xscale_3 = (enum a7xx_bin_scale)util_logbase2(frag_areas[3].width),
                  .yscale_3 = (enum a7xx_bin_scale)util_logbase2(frag_areas[3].height),
                  .xscale_4 = (enum a7xx_bin_scale)util_logbase2(frag_areas[4].width),
                  .yscale_4 = (enum a7xx_bin_scale)util_logbase2(frag_areas[4].height),
                  .xscale_5 = (enum a7xx_bin_scale)util_logbase2(frag_areas[5].width),
                  .yscale_5 = (enum a7xx_bin_scale)util_logbase2(frag_areas[5].height)),
               GRAS_BIN_FOVEAT_OFFSET_0(CHIP,
                  .xoffset_0 = frag_offsets[0].x,
                  .xoffset_1 = frag_offsets[1].x,
                  .xoffset_2 = frag_offsets[2].x),
               GRAS_BIN_FOVEAT_OFFSET_1(CHIP,
                  .xoffset_3 = frag_offsets[3].x,
                  .xoffset_4 = frag_offsets[4].x,
                  .xoffset_5 = frag_offsets[5].x),
               GRAS_BIN_FOVEAT_OFFSET_2(CHIP,
                  .yoffset_0 = frag_offsets[0].y,
                  .yoffset_1 = frag_offsets[1].y,
                  .yoffset_2 = frag_offsets[2].y),
               GRAS_BIN_FOVEAT_OFFSET_3(CHIP,
                  .yoffset_3 = frag_offsets[3].y,
                  .yoffset_4 = frag_offsets[4].y,
                  .yoffset_5 = frag_offsets[5].y));

            tu_cs_emit_regs(cs, RB_BIN_FOVEAT(CHIP,
                  .binscaleen = bin_scale_en));
         } else {
            tu_cs_emit_regs(cs, GRAS_BIN_FOVEAT(CHIP));
            tu_cs_emit_regs(cs, RB_BIN_FOVEAT(CHIP));
         }
      }

      util_dynarray_foreach (&cmd->fdm_bin_patchpoints,
                             struct tu_fdm_bin_patchpoint, patch) {
         tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 2 + patch->size);
         tu_cs_emit_qw(cs, patch->iova);
         patch->apply(cmd, cs, patch->data, (VkOffset2D) { x1, y1 },
                      frag_offsets, views, tile->frag_areas, bins, false);
      }

      /* Make the CP wait until the CP_MEM_WRITE's to the command buffers
       * land. When loading FS params via UBOs, we also need to invalidate
       * UCHE because the FS param patchpoint is read through UCHE.
       */
      tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
      if (cmd->device->compiler->load_shader_consts_via_preamble) {
         tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);
         tu_cs_emit_wfi(cs);
      }
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);
   } else if (cmd->device->physical_device->info->props.has_hw_bin_scaling) {
      tu_cs_emit_regs(cs, GRAS_BIN_FOVEAT(CHIP, 0));
      tu_cs_emit_regs(cs, RB_BIN_FOVEAT(CHIP, 0));
   }
}

template <chip CHIP>
static void
tu6_emit_sysmem_resolve(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t layer_mask,
                        uint32_t a,
                        uint32_t gmem_a)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_image_view *dst = cmd->state.attachments[a];
   const struct tu_image_view *src = cmd->state.attachments[gmem_a];

   tu_resolve_sysmem<CHIP>(cmd, cs, src, dst, layer_mask, fb->layers, &cmd->state.render_area);
}

template <chip CHIP>
static void
tu6_emit_sysmem_resolves(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         const struct tu_subpass *subpass)
{
   if (subpass->resolve_attachments) {
      /* From the documentation for vkCmdNextSubpass, section 7.4 "Render Pass
       * Commands":
       *
       *    End-of-subpass multisample resolves are treated as color
       *    attachment writes for the purposes of synchronization.
       *    This applies to resolve operations for both color and
       *    depth/stencil attachments. That is, they are considered to
       *    execute in the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
       *    pipeline stage and their writes are synchronized with
       *    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT. Synchronization between
       *    rendering within a subpass and any resolve operations at the end
       *    of the subpass occurs automatically, without need for explicit
       *    dependencies or pipeline barriers. However, if the resolve
       *    attachment is also used in a different subpass, an explicit
       *    dependency is needed.
       *
       * We use the CP_BLIT path for sysmem resolves, which is really a
       * transfer command, so we have to manually flush similar to the gmem
       * resolve case. However, a flush afterwards isn't needed because of the
       * last sentence and the fact that we're in sysmem mode.
       */
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_COLOR);
      if (subpass->resolve_depth_stencil)
         tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_DEPTH);

      tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);

      /* Wait for the flushes to land before using the 2D engine */
      tu_cs_emit_wfi(cs);

      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);

         tu6_emit_sysmem_resolve<CHIP>(cmd, cs, subpass->multiview_mask, a, gmem_a);
      }
   }
}

template <chip CHIP>
static void
tu6_emit_sysmem_unresolve(struct tu_cmd_buffer *cmd,
                          struct tu_cs *cs,
                          uint32_t layer_mask,
                          uint32_t a,
                          uint32_t gmem_a)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_image_view *src = cmd->state.attachments[a];
   const struct tu_image_view *dst = cmd->state.attachments[gmem_a];

   tu_resolve_sysmem<CHIP>(cmd, cs, src, dst, layer_mask, fb->layers, &cmd->state.render_area);
}

template <chip CHIP>
static void
tu6_emit_sysmem_unresolves(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           const struct tu_subpass *subpass)
{
   if (subpass->unresolve_count) {
      /* Similar to above, we need to explicitly flush afterwards to keep this
       * in sync with draw commands. However we also don't currently insert
       * dependencies when a resolve is followed by an unresolve so we also
       * need to manually flush for that case.
       */
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_COLOR);
      tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);

      /* Wait for the flushes to land before using the 2D engine */
      tu_cs_emit_wfi(cs);

      bool unresolve_ds = false;
      for (unsigned i = 0; i < subpass->unresolve_count; i++) {
         uint32_t a = subpass->unresolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         if (vk_format_is_depth_or_stencil(cmd->state.pass->attachments[a].format))
            unresolve_ds = true;

         uint32_t gmem_a = tu_subpass_get_attachment_to_unresolve(subpass, i);

         tu6_emit_sysmem_unresolve<CHIP>(cmd, cs, subpass->multiview_mask, a, gmem_a);
      }

      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_COLOR);
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_INVALIDATE_COLOR);
      if (unresolve_ds) {
         tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_DEPTH);
         tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_INVALIDATE_DEPTH);
      }
      tu_cs_emit_wfi(cs);
   }
}
template <chip CHIP>
static void
tu6_emit_gmem_resolves(struct tu_cmd_buffer *cmd,
                       const struct tu_subpass *subpass,
                       struct tu_resolve_group *resolve_group,
                       struct tu_cs *cs)
{
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   if (subpass->resolve_attachments) {
      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);

         tu_store_gmem_attachment<CHIP>(cmd, cs, resolve_group, a, gmem_a,
                                        fb->layers, subpass->multiview_mask,
                                        false);

         if (pass->attachments[a].gmem) {
            /* check if the resolved attachment is needed by later subpasses,
             * if it is, should be doing a GMEM->GMEM resolve instead of
             * GMEM->MEM->GMEM..
             */
            perf_debug(cmd->device,
                       "TODO: missing GMEM->GMEM resolve path\n");
            if (CHIP >= A7XX)
               tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_BLIT_CACHE);
            tu_load_gmem_attachment<CHIP>(cmd, cs, resolve_group, a, a, false, true);
         }
      }
   }
}

/* Emits any tile stores at the end of a subpass.
 *
 * These are emitted into draw_cs for non-final subpasses, and tile_store_cs for
 * the final subpass. The draw_cs ones mean that we have to disable IB2 skipping
 * for the draw_cs so we don't exit before storing.  The separate tile_store_cs
 * lets us leave IB2 skipping enabled in the common case of a single-subpass
 * renderpass (or dynamic rendering).
 *
 * To do better in the multi-subpass case, we'd need the individual CS entries
 * of draw_cs to have a flag for whether they can be skipped or not, and
 * interleave drawing cs entries with store cs entries.
 *
 * This is independent of cond_store_allowed, which is about "can we skip doing
 * the store if no other rendering happened in the tile?"  We can only skip if
 * the cond that we set up at the start of the tile (or reset just before
 * calling tile_store_cs) is still in place.
 */
template <chip CHIP>
static void
tu6_emit_gmem_stores(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     struct tu_resolve_group *resolve_group,
                     const struct tu_subpass *subpass)
{
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, cmd->state.tiling);
   uint32_t subpass_idx = subpass - cmd->state.pass->subpasses;
   const bool cond_exec_allowed = vsc->binning_possible &&
                                  cmd->state.pass->has_cond_load_store &&
                                  (!cmd->state.rp.draw_cs_writes_to_cond_pred ||
                                  cs != &cmd->draw_cs);

   bool scissor_emitted = false;

   /* Resolve should happen before store in case BLIT_EVENT_STORE_AND_CLEAR is
    * used for a store.
    *
    * Note that we're emitting the resolves into the tile store CS, which is
    * unconditionally executed (unlike draw_cs which depends on geometry having
    * been generated).  a7xx has HW conditional resolve support that may skip
    * the resolve if geometry didn't cover it, anyway.
    */
   if (subpass->resolve_attachments) {
      if (!scissor_emitted) {
         tu6_emit_blit_scissor(cmd, cs, true, false);
         scissor_emitted = true;
      }
      tu6_emit_gmem_resolves<CHIP>(cmd, subpass, resolve_group, cs);
   }

   for (uint32_t a = 0; a < pass->attachment_count; ++a) {
      const struct tu_render_pass_attachment *att = &pass->attachments[a];
      /* Note: att->cond_store_allowed implies at least one of att->store_* set */
      if (pass->attachments[a].gmem && att->last_subpass_idx == subpass_idx) {
         if (!scissor_emitted) {
            tu6_emit_blit_scissor(cmd, cs, true, false);
            scissor_emitted = true;
         }
         tu_store_gmem_attachment<CHIP>(cmd, cs, resolve_group, a, a,
                                  fb->layers, att->used_views,
                                  cond_exec_allowed);
      }
   }
}

template <chip CHIP>
static void
tu6_emit_tile_store_cs(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_render_pass *pass = cmd->state.pass;
   uint32_t subpass_idx = pass->subpass_count - 1;
   const struct tu_subpass *subpass = &pass->subpasses[subpass_idx];

   if (pass->has_fdm)
      tu_cs_set_writeable(cs, true);

   /* We believe setting the marker affects what state HW blocks save/restore
    * during preemption.  So we only emit it before the stores at the end of the
    * last subpass, not other resolves.
    */
   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BIN_RESOLVE) |
                  A6XX_CP_SET_MARKER_0_USES_GMEM);

   struct tu_resolve_group resolve_group = {};

   tu6_emit_gmem_stores<CHIP>(cmd, cs, &resolve_group, subpass);

   tu_emit_resolve_group<CHIP>(cmd, cs, &resolve_group);

   if (pass->has_fdm)
      tu_cs_set_writeable(cs, false);

}

void
tu_disable_draw_states(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   cmd->state.dirty |= TU_CMD_DIRTY_DRAW_STATE;
}

template <chip CHIP>
static void
tu6_init_static_regs(struct tu_device *dev, struct tu_cs *cs)
{
   const struct tu_physical_device *phys_dev = dev->physical_device;

   if (CHIP >= A7XX) {
      /* On A7XX, RB_CCU_CNTL was broken into two registers, RB_CCU_CNTL which has
       * static properties that can be set once, this requires a WFI to take effect.
       * While the newly introduced register RB_CCU_CACHE_CNTL has properties that may
       * change per-RP and don't require a WFI to take effect, only CCU inval/flush
       * events are required.
       */

      enum a7xx_concurrent_resolve_mode resolve_mode = CONCURRENT_RESOLVE_MODE_2;
      if (TU_DEBUG(NO_CONCURRENT_RESOLVES))
         resolve_mode = CONCURRENT_RESOLVE_MODE_DISABLED;

      enum a7xx_concurrent_unresolve_mode unresolve_mode = CONCURRENT_UNRESOLVE_MODE_FULL;
      if (TU_DEBUG(NO_CONCURRENT_UNRESOLVES))
         unresolve_mode = CONCURRENT_UNRESOLVE_MODE_DISABLED;

      tu_cs_emit_regs(cs, RB_CCU_CNTL(A7XX,
         .gmem_fast_clear_disable =
           !dev->physical_device->info->props.has_gmem_fast_clear,
         .concurrent_resolve_mode = resolve_mode,
         .concurrent_unresolve_mode = unresolve_mode,
      ));
   }

   for (size_t i = 0; i < ARRAY_SIZE(phys_dev->info->magic_raw); i++) {
      auto magic_reg = phys_dev->info->magic_raw[i];
      if (!magic_reg.reg)
         break;

      uint32_t value = magic_reg.value;
      switch(magic_reg.reg) {
         case REG_A6XX_TPL1_DBG_ECO_CNTL:
            value = (value & ~A6XX_TPL1_DBG_ECO_CNTL_LINEAR_MIPMAP_FALLBACK_IN_BLOCKS) |
                    (phys_dev->info->props.supports_linear_mipmap_threshold_in_blocks
                        ? A6XX_TPL1_DBG_ECO_CNTL_LINEAR_MIPMAP_FALLBACK_IN_BLOCKS
                        : 0);
            break;
         case REG_A6XX_TPL1_DBG_ECO_CNTL1:
            value = (value & ~A6XX_TPL1_DBG_ECO_CNTL1_TP_UBWC_FLAG_HINT) |
                    (phys_dev->info->props.enable_tp_ubwc_flag_hint
                        ? A6XX_TPL1_DBG_ECO_CNTL1_TP_UBWC_FLAG_HINT
                        : 0);
            break;
      }

      tu_cs_emit_write_reg(cs, magic_reg.reg, value);
   }

   if (dev->physical_device->info->props.has_attachment_shading_rate) {
      tu_cs_emit_write_reg(cs, REG_A7XX_GRAS_LRZ_QUALITY_LOOKUP_TABLE(0),
                           fd_gras_shading_rate_lut(0));
      tu_cs_emit_write_reg(cs, REG_A7XX_GRAS_LRZ_QUALITY_LOOKUP_TABLE(1),
                           fd_gras_shading_rate_lut(1));
   }

   tu_cs_emit_write_reg(cs, REG_A6XX_SP_NC_MODE_CNTL_2, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_PERFCTR_SHADER_MASK, 0x3f);
   if (CHIP == A6XX && !cs->device->physical_device->info->props.is_a702)
      tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_UNKNOWN_B605, 0x44);
   if (CHIP == A6XX) {
      tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
      tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE01, 0);
   }

   tu_cs_emit_write_reg(cs, REG_A6XX_SP_GFX_USIZE, 0); // 2 on a740 ???
   tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_PS_ROTATION_CNTL, 0);
   if (CHIP == A6XX)
      tu_cs_emit_regs(cs, HLSQ_SHARED_CONSTS(CHIP, .enable = false));
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_A9A8, 0);
   tu_cs_emit_regs(cs, A6XX_SP_MODE_CNTL(.constant_demotion_enable = true,
                                            .isammode = ISAMMODE_GL,
                                            .shared_consts_enable = false));

   tu_cs_emit_regs(cs, A6XX_VFD_MODE_CNTL(.vertex = true, .instance = true));
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_MODE_CNTL, 0x00000010);

   tu_cs_emit_regs(cs, GRAS_MODE_CNTL(CHIP, CHIP >= A7XX ? 0x2 : 0));

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8818, 0);

   if (CHIP == A6XX) {
      tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8819, 0);
      tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881A, 0);
      tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881B, 0);
      tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881C, 0);
      tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881D, 0);
      tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881E, 0);
   }

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_88F0, 0);

   tu_cs_emit_regs(cs, VPC_REPLACE_MODE_CNTL(CHIP, false));
   tu_cs_emit_regs(cs, VPC_ROTATION_CNTL(CHIP));

   tu_cs_emit_regs(cs, VPC_SO_OVERRIDE(CHIP, true));

   tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_PS_SWIZZLE_CNTL, 0);

   tu_cs_emit_regs(cs, GRAS_SC_SCREEN_SCISSOR_CNTL(CHIP));
   if (CHIP == A6XX) {
      tu_cs_emit_regs(cs, GRAS_SU_CONSERVATIVE_RAS_CNTL(CHIP, 0));
      tu_cs_emit_regs(cs, PC_DGEN_SU_CONSERVATIVE_RAS_CNTL(CHIP));

      tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9210, 0);
      tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9211, 0);
   }
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_LB_MODE_CNTL, 0);
   tu_cs_emit_regs(cs, PC_CONTEXT_SWITCH_GFX_PREEMPTION_MODE(CHIP));
   tu_cs_emit_regs(cs, A6XX_TPL1_MODE_CNTL(.isammode = ISAMMODE_GL,
                                            .texcoordroundmode = dev->instance->use_tex_coord_round_nearest_even_mode
                                               ? COORD_ROUND_NEAREST_EVEN
                                               : COORD_TRUNCATE,
                                            .nearestmipsnap = CLAMP_ROUND_TRUNCATE,
                                            .destdatatypeoverride = true));
   tu_cs_emit_regs(cs, SP_REG_PROG_ID_3(CHIP, .dword = 0xfc));

   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_RENDER_MODE, 0x00000000);

   tu_cs_emit_regs(cs, A6XX_RB_ALPHA_TEST_CNTL()); /* always disable alpha test */

   tu_cs_emit_regs(cs,
                   A6XX_TPL1_GFX_BORDER_COLOR_BASE(.bo = dev->global_bo,
                                                     .bo_offset = gb_offset(bcolor)));
   tu_cs_emit_regs(cs,
                   A6XX_TPL1_CS_BORDER_COLOR_BASE(.bo = dev->global_bo,
                                                        .bo_offset = gb_offset(bcolor)));

   /* BR-only registers */
   if (CHIP >= A7XX)
      tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) |
                             CP_COND_REG_EXEC_0_BR);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_DBG_ECO_CNTL,
                        phys_dev->info->magic.RB_DBG_ECO_CNTL);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_RBP_CNTL,
                        phys_dev->info->magic.RB_RBP_CNTL);
   if (CHIP >= A7XX) {
      tu_cs_emit_regs(cs, RB_UNKNOWN_8E09(CHIP, 0x7));
      tu_cond_exec_end(cs);
   }

   if (CHIP == A7XX) {
      tu_cs_emit_regs(cs, TPL1_BICUBIC_WEIGHTS_TABLE_REG(CHIP, 0, 0),
                      TPL1_BICUBIC_WEIGHTS_TABLE_REG(CHIP, 1, 0x3fe05ff4),
                      TPL1_BICUBIC_WEIGHTS_TABLE_REG(CHIP, 2, 0x3fa0ebee),
                      TPL1_BICUBIC_WEIGHTS_TABLE_REG(CHIP, 3, 0x3f5193ed),
                      TPL1_BICUBIC_WEIGHTS_TABLE_REG(CHIP, 4, 0x3f0243f0), );
   }

   if (CHIP >= A7XX) {
      /* Blob sets these two per draw. */
      tu_cs_emit_regs(cs, PC_HS_BUFFER_SIZE(CHIP, TU_TESS_PARAM_SIZE));
      /* Blob adds a bit more space ({0x10, 0x20, 0x30, 0x40} bytes)
       * but the meaning of this additional space is not known,
       * so we play safe and don't add it.
       */
      tu_cs_emit_regs(cs, PC_TF_BUFFER_SIZE(CHIP, TU_TESS_FACTOR_SIZE));
   }

   /* There is an optimization to skip executing draw states for draws with no
    * instances. Instead of simply skipping the draw, internally the firmware
    * sets a bit in PC_DRAW_INITIATOR that seemingly skips the draw. However
    * there is a hardware bug where this bit does not always cause the FS
    * early preamble to be skipped. Because the draw states were skipped,
    * SP_PS_CNTL_0, SP_PS_BASE and so on are never updated and a
    * random FS preamble from the last draw is executed. If the last visible
    * draw is from the same submit, it shouldn't be a problem because we just
    * re-execute the same preamble and preambles don't have side effects, but
    * if it's from another process then we could execute a garbage preamble
    * leading to hangs and faults. To make sure this doesn't happen, we reset
    * SP_PS_CNTL_0 here, making sure that the EARLYPREAMBLE bit isn't set
    * so any leftover early preamble doesn't get executed. Other stages don't
    * seem to be affected.
    */
   if (phys_dev->info->props.has_early_preamble) {
      tu_cs_emit_regs(cs, A6XX_SP_PS_CNTL_0());
   }

   /* Workaround for draw state with constlen not being applied for
    * zero-instance draw calls. See IR3_CONST_ALLOC_DRIVER_PARAMS allocation
    * for more info.
    */
   tu_cs_emit_pkt4(
      cs, CHIP == A6XX ? REG_A6XX_SP_VS_CONST_CONFIG : REG_A7XX_SP_VS_CONST_CONFIG, 1);
   tu_cs_emit(cs, A6XX_SP_VS_CONST_CONFIG_CONSTLEN(8) | A6XX_SP_VS_CONST_CONFIG_ENABLED);
}

/* Emit the bin restore preamble, which runs in between bins when L1
 * preemption with skipsaverestore happens and we switch back to this context.
 * We need to restore static registers normally programmed at cmdbuf start
 * which weren't saved, and we need to program the CCU state which is normally
 * programmed before rendering the bins and isn't saved/restored by the CP
 * because it is always the same for GMEM render passes.
 */
template <chip CHIP>
static void
tu_emit_bin_preamble(struct tu_device *dev, struct tu_cs *cs, bool bv)
{
   tu6_init_static_regs<CHIP>(dev, cs);

   if (!bv)
      emit_rb_ccu_cntl<CHIP>(cs, dev, true);
   emit_vpc_attr_buf<CHIP>(cs, dev, true);

   if (CHIP == A7XX && !bv) {
      tu7_emit_tile_render_begin_regs<CHIP>(cs);
   }

   if (CHIP == A6XX) {
      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_VSC_CHANNEL_VISIBILITY(0)) |
                     CP_MEM_TO_REG_0_CNT(32));
      tu_cs_emit_qw(cs, dev->global_bo->iova + gb_offset(vsc_state));
   }
}

VkResult
tu_init_bin_preamble(struct tu_device *device)
{
   struct tu_cs preamble_cs;
   VkResult result = tu_cs_begin_sub_stream(&device->sub_cs, 256, &preamble_cs);
   if (result != VK_SUCCESS)
      return vk_startup_errorf(device->instance, result, "bin restore");

   TU_CALLX(device, tu_emit_bin_preamble)(device, &preamble_cs, false);

   device->bin_preamble_entry = tu_cs_end_sub_stream(&device->sub_cs, &preamble_cs);

   if (device->physical_device->info->chip >= 7) {
      result = tu_cs_begin_sub_stream(&device->sub_cs, 256, &preamble_cs);
      if (result != VK_SUCCESS)
         return vk_startup_errorf(device->instance, result, "bin restore");

      TU_CALLX(device, tu_emit_bin_preamble)(device, &preamble_cs, true);

      device->bin_preamble_bv_entry = tu_cs_end_sub_stream(&device->sub_cs, &preamble_cs);
   }

   return VK_SUCCESS;
}

template <chip CHIP>
static void
tu6_init_hw(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_device *dev = cmd->device;
   const struct tu_physical_device *phys_dev = dev->physical_device;

   if (CHIP == A6XX) {
      tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);
   } else {
      tu7_thread_control(cs, CP_SET_THREAD_BOTH);

      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_INVALIDATE_COLOR);
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_INVALIDATE_DEPTH);
      tu_emit_event_write<CHIP>(cmd, cs, FD_LRZ_INVALIDATE);
      tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);
      tu_cs_emit_wfi(cs);
   }

   tu_cs_emit_regs(cs, SP_UPDATE_CNTL(CHIP,
         .vs_state = true,
         .hs_state = true,
         .ds_state = true,
         .gs_state = true,
         .fs_state = true,
         .cs_state = true,
         .cs_uav = true,
         .gfx_uav = true,
         .cs_shared_const = true,
         .gfx_shared_const = true,
         .cs_bindless = CHIP == A6XX ? 0x1f : 0xff,
         .gfx_bindless = CHIP == A6XX ? 0x1f : 0xff,));

   tu_cs_emit_wfi(cs);

   if (dev->dbg_cmdbuf_stomp_cs) {
      tu_cs_emit_call(cs, dev->dbg_cmdbuf_stomp_cs);
   }

   cmd->state.cache.pending_flush_bits &=
      ~(TU_CMD_FLAG_WAIT_FOR_IDLE | TU_CMD_FLAG_CACHE_INVALIDATE);

   tu6_init_static_regs<CHIP>(cmd->device, cs);

   emit_rb_ccu_cntl<CHIP>(cs, cmd->device, false);
   emit_vpc_attr_buf<CHIP>(cs, cmd->device, false);
   cmd->state.ccu_state = TU_CMD_CCU_SYSMEM;

   tu_disable_draw_states(cmd, cs);

   if (phys_dev->info->props.cmdbuf_start_a725_quirk) {
      tu_cs_reserve(cs, 3 + 4);
      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) |
                     CP_COND_REG_EXEC_0_BR | CP_COND_REG_EXEC_0_LPAC);
      tu_cs_emit(cs, RENDER_MODE_CP_COND_REG_EXEC_1_DWORDS(4));
      tu_cs_emit_ib(cs, &dev->cmdbuf_start_a725_quirk_entry);
   }

   if (CHIP >= A7XX) {
      if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
         tu_cs_set_writeable(cs, true);

      /* This sets the amount BV is allowed to be ahead of BR when we do
       * BV_WAIT_FOR_BR. By setting it based on the vis stream count we
       * prevent write-after-read races with the vis stream.
       */
      tu_cs_emit_pkt7(cs, CP_BV_BR_COUNT_OPS, 2);
      tu_cs_emit(cs, CP_BV_BR_COUNT_OPS_0_OP(PIPE_SET_BR_OFFSET));

      struct tu_vis_stream_patchpoint *patchpoint =
         &cmd->vis_stream_count_patchpoint;
      patchpoint->data = cs->cur;
      patchpoint->iova = tu_cs_get_cur_iova(cs);
      tu_cs_emit(cs, 1);

      if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
         tu_cs_set_writeable(cs, false);

      tu7_set_thread_br_patchpoint(cmd, cs, false);
   }

   tu_cs_emit_pkt7(cs, CP_SET_AMBLE, 3);
   tu_cs_emit_qw(cs, cmd->device->bin_preamble_entry.bo->iova +
                     cmd->device->bin_preamble_entry.offset);
   tu_cs_emit(cs, CP_SET_AMBLE_2_DWORDS(cmd->device->bin_preamble_entry.size /
                                        sizeof(uint32_t)) |
                  CP_SET_AMBLE_2_TYPE(BIN_PREAMBLE_AMBLE_TYPE));

   if (CHIP >= A7XX) {
      tu7_thread_control(cs, CP_SET_THREAD_BV);

      tu_cs_emit_pkt7(cs, CP_SET_AMBLE, 3);
      tu_cs_emit_qw(cs, cmd->device->bin_preamble_bv_entry.bo->iova +
                        cmd->device->bin_preamble_bv_entry.offset);
      tu_cs_emit(cs, CP_SET_AMBLE_2_DWORDS(cmd->device->bin_preamble_bv_entry.size /
                                           sizeof(uint32_t)) |
                     CP_SET_AMBLE_2_TYPE(BIN_PREAMBLE_AMBLE_TYPE));

      tu7_thread_control(cs, CP_SET_THREAD_BOTH);

      tu7_set_pred_mask(cs, (1u << TU_PREDICATE_VTX_STATS_RUNNING) |
                            (1u << TU_PREDICATE_VTX_STATS_NOT_RUNNING),
                            (1u << TU_PREDICATE_VTX_STATS_NOT_RUNNING));
   }

   tu_cs_emit_pkt7(cs, CP_SET_AMBLE, 3);
   tu_cs_emit_qw(cs, 0);
   tu_cs_emit(cs, CP_SET_AMBLE_2_TYPE(PREAMBLE_AMBLE_TYPE));

   tu_cs_emit_pkt7(cs, CP_SET_AMBLE, 3);
   tu_cs_emit_qw(cs, 0);
   tu_cs_emit(cs, CP_SET_AMBLE_2_TYPE(POSTAMBLE_AMBLE_TYPE));

   if (CHIP >= A7XX) {
      tu7_set_thread_br_patchpoint(cmd, cs, false);
   }

   tu_cs_sanity_check(cs);
}

bool
tu_enable_fdm_offset(struct tu_cmd_buffer *cmd)
{
   if (!cmd->state.pass)
      return false;

   if (!cmd->state.pass->has_fdm)
      return false;

   unsigned fdm_a = cmd->state.pass->fragment_density_map.attachment;
   if (fdm_a == VK_ATTACHMENT_UNUSED)
      return TU_DEBUG(FDM_OFFSET);

   const struct tu_image_view *fdm = cmd->state.attachments[fdm_a];
   return fdm->image->vk.create_flags &
      VK_IMAGE_CREATE_FRAGMENT_DENSITY_MAP_OFFSET_BIT_EXT;
}

template <chip CHIP>
static void
update_vsc_pipe(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                uint32_t num_vsc_pipes)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, tiling);

   tu_cs_emit_regs(cs,
                   VSC_BIN_SIZE(CHIP, .binw = tiling->tile0.width,
                                     .binh = tiling->tile0.height));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_EXPANDED_BIN_CNTL(.nx = vsc->tile_count.width,
                                              .ny = vsc->tile_count.height));

   tu_cs_emit_pkt4(cs, REG_A6XX_VSC_PIPE_CONFIG_REG(0), num_vsc_pipes);
   tu_cs_emit_array(cs, vsc->pipe_config, num_vsc_pipes);

   tu_cs_emit_regs(cs,
                   A6XX_VSC_PIPE_DATA_PRIM_STRIDE(cmd->vsc_prim_strm_pitch),
                   A6XX_VSC_PIPE_DATA_PRIM_LENGTH(cmd->vsc_prim_strm_pitch - VSC_PAD));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_PIPE_DATA_DRAW_STRIDE(cmd->vsc_draw_strm_pitch),
                   A6XX_VSC_PIPE_DATA_DRAW_LENGTH(cmd->vsc_draw_strm_pitch - VSC_PAD));

   if (CHIP >= A7XX)
      tu_cs_emit_regs(cs, VSC_UNKNOWN_0D08(CHIP, 0));
}

static void
emit_vsc_overflow_test(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, tiling);
   const uint32_t used_pipe_count =
      vsc->pipe_count.width * vsc->pipe_count.height;

   for (int i = 0; i < used_pipe_count; i++) {
      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, REG_A6XX_VSC_PIPE_DATA_DRAW_SIZE(i));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_draw_strm_pitch - VSC_PAD));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, global_iova(cmd, vsc_draw_overflow));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(cmd->vsc_draw_strm_pitch));

      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, REG_A6XX_VSC_PIPE_DATA_PRIM_SIZE(i));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_prim_strm_pitch - VSC_PAD));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, global_iova(cmd, vsc_prim_overflow));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(cmd->vsc_prim_strm_pitch));
   }

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
}

template <chip CHIP>
static void
tu6_emit_binning_pass(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      const VkOffset2D *fdm_offsets, bool use_cb)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_tiling_config *tiling = cmd->state.tiling;

   /* If this command buffer may be executed multiple times, then
    * viewports/scissor states may have been changed by previous executions
    * and we need to reset them before executing the binning IB. With FDM
    * offset the viewport also needs to be transformed during the binning
    * phase.
    */
   if ((!(cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) ||
        fdm_offsets) && cmd->fdm_bin_patchpoints.size != 0) {
      unsigned num_views = tu_fdm_num_layers(cmd);
      VkExtent2D unscaled_frag_areas[num_views];
      VkRect2D bins[num_views];
      VkOffset2D frag_offsets[num_views];
      for (unsigned i = 0; i < num_views; i++) {
         unscaled_frag_areas[i] = (VkExtent2D) { 1, 1 };
         frag_offsets[i] = (VkOffset2D) { 0, 0 };
         if (fdm_offsets && !cmd->state.rp.shared_viewport) {
            /* We need to shift over the viewport and scissor during the
             * binning pass to match the shift applied when rendering. The way
             * to do this is to make the per-view bin start negative. In the
             * actual rendering pass, the per-view bin start is shifted in a
             * negative direction but the first bin is clipped so that the bin
             * start is never negative, but we need to do this to avoid
             * clipping the user scissor to a non-zero common bin start. We
             * skip patching load/store below in order to avoid patching loads
             * and stores to a crazy negative-offset bin. The parts of the
             * framebuffer left or above the origin correspond to the
             * non-visible parts of the left or top bins that will be
             * discarded. The framebuffer still needs to extend to the
             * original bottom and right, to avoid incorrectly clipping the
             * user scissor, so we need to add to the width and height to
             * compensate.
             */
            VkOffset2D bin_offset = tu_bin_offset(fdm_offsets[i], tiling);
            bins[i] = {
               { -bin_offset.x, -bin_offset.y },
               { fb->width + bin_offset.x, fb->height + bin_offset.y },
            };
         } else {
            bins[i] = { { 0, 0 }, { fb->width, fb->height } };
         }
      }
      util_dynarray_foreach (&cmd->fdm_bin_patchpoints,
                             struct tu_fdm_bin_patchpoint, patch) {
         if (patch->flags & TU_FDM_SKIP_BINNING)
            continue;
         tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 2 + patch->size);
         tu_cs_emit_qw(cs, patch->iova);
         patch->apply(cmd, cs, patch->data, (VkOffset2D) {0, 0}, frag_offsets,
                      num_views, unscaled_frag_areas, bins, true);
      }

      tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);
   }

   uint32_t width = fb->width + (fdm_offsets ? tiling->tile0.width : 0);
   uint32_t height = fb->height + (fdm_offsets ? tiling->tile0.height : 0);

   tu6_emit_window_scissor<CHIP>(cs, 0, 0, width - 1, height - 1);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BIN_VISIBILITY));

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_regs(cs,
                   A6XX_VFD_RENDER_MODE(.render_mode = BINNING_PASS));

   update_vsc_pipe<CHIP>(cmd, cs, phys_dev->info->num_vsc_pipes);

   tu_emit_event_write<CHIP>(cmd, cs, FD_VSC_BINNING_START);

   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET(.x = 0, .y = 0));

   tu_cs_emit_regs(cs,
                   A6XX_TPL1_WINDOW_OFFSET(.x = 0, .y = 0));

   if (use_cb)
      trace_start_concurrent_binning_ib(&cmd->trace, cs, cmd);
   else
      trace_start_binning_ib(&cmd->trace, cs, cmd);

   /* emit IB to binning drawcmds: */
   tu_cs_emit_call(cs, &cmd->draw_cs);

   if (use_cb)
      trace_end_concurrent_binning_ib(&cmd->trace, cs);
   else
      trace_end_binning_ib(&cmd->trace, cs);

   tu_clone_trace_range(cmd, cs, &cmd->trace, cmd->trace_renderpass_start,
                        u_trace_end_iterator(&cmd->rp_trace));

   /* switching from binning pass to GMEM pass will cause a switch from
    * PROGRAM_BINNING to PROGRAM, which invalidates const state (XS_CONST states)
    * so make sure these states are re-emitted
    * (eventually these states shouldn't exist at all with shader prologue)
    * only VS and GS are invalidated, as FS isn't emitted in binning pass,
    * and we don't use HW binning when tesselation is used
    */
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                  CP_SET_DRAW_STATE__0_DISABLE |
                  CP_SET_DRAW_STATE__0_GROUP_ID(TU_DRAW_STATE_CONST));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   tu_emit_event_write<CHIP>(cmd, cs, FD_VSC_BINNING_END);

   /* This flush is probably required because the VSC, which produces the
    * visibility stream, is a client of UCHE, whereas the CP needs to read the
    * visibility stream (without caching) to do draw skipping. The
    * WFI+WAIT_FOR_ME combination guarantees that the binning commands
    * submitted are finished before reading the VSC regs (in
    * emit_vsc_overflow_test) or the VSC_DATA buffer directly (implicitly as
    * part of draws).
    */
   tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_CLEAN);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   emit_vsc_overflow_test(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x0);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);
}

static struct tu_draw_state
tu_emit_input_attachments(struct tu_cmd_buffer *cmd,
                          const struct tu_subpass *subpass,
                          bool gmem)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   uint32_t layers = MAX2(cmd->state.framebuffer->layers,
                          cmd->state.pass->num_views);

   /* note: we can probably emit input attachments just once for the whole
    * renderpass, this would avoid emitting both sysmem/gmem versions
    *
    * emit two texture descriptors for each input, as a workaround for
    * d24s8/d32s8, which can be sampled as both float (depth) and integer (stencil)
    * tu_shader lowers uint input attachment loads to use the 2nd descriptor
    * in the pair
    * TODO: a smarter workaround
    */

   if (!subpass->input_count)
      return (struct tu_draw_state) {};

   struct tu_cs_memory texture;
   VkResult result = tu_cs_alloc(&cmd->sub_cs, subpass->input_count * 2,
                                 A6XX_TEX_CONST_DWORDS, &texture);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return (struct tu_draw_state) {};
   }

   for (unsigned i = 0; i < subpass->input_count * 2; i++) {
      uint32_t a = subpass->input_attachments[i / 2].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = cmd->state.attachments[a];
      const struct tu_render_pass_attachment *att =
         &cmd->state.pass->attachments[a];
      uint32_t dst[A6XX_TEX_CONST_DWORDS];
      uint32_t gmem_offset = tu_attachment_gmem_offset(cmd, att, 0);
      uint32_t cpp = att->cpp;

      memcpy(dst, iview->view.descriptor, A6XX_TEX_CONST_DWORDS * 4);

      /* Cube descriptors require a different sampling instruction in shader,
       * however we don't know whether image is a cube or not until the start
       * of a renderpass. We have to patch the descriptor to make it compatible
       * with how it is sampled in shader.
       */
      enum a6xx_tex_type tex_type =
         (enum a6xx_tex_type) pkt_field_get(A6XX_TEX_CONST_2_TYPE, dst[2]);
      if (tex_type == A6XX_TEX_CUBE) {
         dst[2] = pkt_field_set(A6XX_TEX_CONST_2_TYPE, dst[2], A6XX_TEX_2D);

         uint32_t depth = pkt_field_get(A6XX_TEX_CONST_5_DEPTH, dst[5]);
         dst[5] = pkt_field_set(A6XX_TEX_CONST_5_DEPTH, dst[5], depth * 6);
      }

      if (i % 2 == 1 && att->format == VK_FORMAT_D24_UNORM_S8_UINT) {
         /* note this works because spec says fb and input attachments
          * must use identity swizzle
          *
          * Also we clear swap to WZYX.  This is because the view might have
          * picked XYZW to work better with border colors.
          */
         dst[0] &= ~(A6XX_TEX_CONST_0_FMT__MASK |
            A6XX_TEX_CONST_0_SWAP__MASK |
            A6XX_TEX_CONST_0_SWIZ_X__MASK | A6XX_TEX_CONST_0_SWIZ_Y__MASK |
            A6XX_TEX_CONST_0_SWIZ_Z__MASK | A6XX_TEX_CONST_0_SWIZ_W__MASK);
         if (!cmd->device->physical_device->info->props.has_z24uint_s8uint) {
            dst[0] |= A6XX_TEX_CONST_0_FMT(FMT6_8_8_8_8_UINT) |
               A6XX_TEX_CONST_0_SWIZ_X(A6XX_TEX_W) |
               A6XX_TEX_CONST_0_SWIZ_Y(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_Z(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_W(A6XX_TEX_ONE);
         } else {
            dst[0] |= A6XX_TEX_CONST_0_FMT(FMT6_Z24_UINT_S8_UINT) |
               A6XX_TEX_CONST_0_SWIZ_X(A6XX_TEX_Y) |
               A6XX_TEX_CONST_0_SWIZ_Y(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_Z(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_W(A6XX_TEX_ONE);
         }
      }

      if (i % 2 == 1 && att->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         dst[0] = pkt_field_set(A6XX_TEX_CONST_0_FMT, dst[0], FMT6_8_UINT);
         dst[2] = pkt_field_set(A6XX_TEX_CONST_2_PITCHALIGN, dst[2], 0);
         dst[2] = pkt_field_set(A6XX_TEX_CONST_2_PITCH, dst[2],
                                iview->stencil_pitch);
         dst[3] = 0;
         dst[4] = iview->stencil_base_addr;
         dst[5] = (dst[5] & 0xffff) | iview->stencil_base_addr >> 32;

         cpp = att->samples;
         gmem_offset = att->gmem_offset_stencil[cmd->state.gmem_layout];
      }

      if (!gmem || !subpass->input_attachments[i / 2].patch_input_gmem ||
          /* Skip GMEM patching when tiling is impossible as we may get
           * assertion failures from register packing below.
           */
          !tiling->possible) {
         memcpy(&texture.map[i * A6XX_TEX_CONST_DWORDS], dst, sizeof(dst));
         continue;
      }

      /* patched for gmem */
      dst[0] = pkt_field_set(A6XX_TEX_CONST_0_TILE_MODE, dst[0], TILE6_2);
      if (!iview->view.is_mutable)
         dst[0] = pkt_field_set(A6XX_TEX_CONST_0_SWAP, dst[0], WZYX);

      /* If FDM offset is used, the last row and column extend beyond the
       * framebuffer but are shifted over when storing. Expand the width and
       * height to account for that.
       */
      if (tu_enable_fdm_offset(cmd)) {
         uint32_t width = pkt_field_get(A6XX_TEX_CONST_1_WIDTH, dst[1]);
         uint32_t height = pkt_field_get(A6XX_TEX_CONST_1_HEIGHT, dst[1]);
         width += cmd->state.tiling->tile0.width;
         height += cmd->state.tiling->tile0.height;
         dst[1] = pkt_field_set(A6XX_TEX_CONST_1_WIDTH, dst[1], width);
         dst[1] = pkt_field_set(A6XX_TEX_CONST_1_HEIGHT, dst[1], height);
      }

      dst[2] =
         A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D) |
         A6XX_TEX_CONST_2_PITCH(tiling->tile0.width * cpp);
      /* Note: it seems the HW implicitly calculates the array pitch, except
       * when rendering to sysmem (i.e. in a custom resolve subpass). We only
       * guarantee the pitch is valid when there is more than 1 layer, so skip
       * emitting it otherwise to avoid asserts.
       */
      if (layers > 1) {
         dst[3] = A6XX_TEX_CONST_3_ARRAY_PITCH(tiling->tile0.width *
                                               tiling->tile0.height * cpp);
      } else {
         dst[3] = 0;
      }
      dst[4] = cmd->device->physical_device->gmem_base + gmem_offset;
      dst[5] &= A6XX_TEX_CONST_5_DEPTH__MASK;
      for (unsigned i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
         dst[i] = 0;

      memcpy(&texture.map[i * A6XX_TEX_CONST_DWORDS], dst, sizeof(dst));
   }

   struct tu_cs cs;
   struct tu_draw_state ds = tu_cs_draw_state(&cmd->sub_cs, &cs, 9);

   tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_FRAG, 3);
   tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_TEX) |
                  CP_LOAD_STATE6_0_NUM_UNIT(subpass->input_count * 2));
   tu_cs_emit_qw(&cs, texture.iova);

   tu_cs_emit_regs(&cs, A6XX_SP_PS_TEXMEMOBJ_BASE(.qword = texture.iova));

   tu_cs_emit_regs(&cs, A6XX_SP_PS_TSIZE(subpass->input_count * 2));

   assert(cs.cur == cs.end); /* validate draw state size */

   return ds;
}

static void
tu_set_input_attachments(struct tu_cmd_buffer *cmd, const struct tu_subpass *subpass)
{
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 6);
   tu_cs_emit_draw_state(cs, TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM,
                         tu_emit_input_attachments(cmd, subpass, true));
   tu_cs_emit_draw_state(cs, TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM,
                         tu_emit_input_attachments(cmd, subpass, false));
}

static void
tu_trace_start_render_pass(struct tu_cmd_buffer *cmd)
{
   if (!u_trace_enabled(&cmd->device->trace_context))
      return;

   uint32_t load_cpp = 0;
   uint32_t store_cpp = 0;
   uint32_t clear_cpp = 0;
   bool has_depth = false;
   char ubwc[MAX_RTS + 3];
   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; i++) {
      const struct tu_render_pass_attachment *attachment =
         &cmd->state.pass->attachments[i];
      if (attachment->load) {
         load_cpp += attachment->cpp;
      }

      if (attachment->store) {
         store_cpp += attachment->cpp;
      }

      if (attachment->clear_mask) {
         clear_cpp += attachment->cpp;
      }

      has_depth |= vk_format_has_depth(attachment->format);
   }

   uint8_t ubwc_len = 0;
   const struct tu_subpass *subpass = &cmd->state.pass->subpasses[0];
   for (uint32_t i = 0; i < subpass->color_count; i++) {
      uint32_t att = subpass->color_attachments[i].attachment;
      ubwc[ubwc_len++] = att == VK_ATTACHMENT_UNUSED ? '-'
                         : cmd->state.attachments[att]->view.ubwc_enabled
                            ? 'y'
                            : 'n';
   }
   if (subpass->depth_used) {
      ubwc[ubwc_len++] = '|';
      ubwc[ubwc_len++] =
         cmd->state.attachments[subpass->depth_stencil_attachment.attachment]
               ->view.ubwc_enabled
            ? 'y'
            : 'n';
   }
   ubwc[ubwc_len] = '\0';

   uint32_t max_samples = 0;
   for (uint32_t i = 0; i < cmd->state.pass->subpass_count; i++) {
      max_samples = MAX2(max_samples, cmd->state.pass->subpasses[i].samples);
   }

   trace_start_render_pass(&cmd->trace, &cmd->cs, cmd, cmd->state.framebuffer,
                           cmd->state.tiling, max_samples, clear_cpp,
                           load_cpp, store_cpp, has_depth, ubwc,
                           cmd->state.rp.cb_disable_reason ? cmd->state.rp.cb_disable_reason : "");
}

template <chip CHIP>
static void
tu_trace_end_render_pass(struct tu_cmd_buffer *cmd, bool gmem)
{
   if (!u_trace_enabled(&cmd->device->trace_context))
      return;

   uint32_t avg_per_sample_bandwidth =
      cmd->state.rp.drawcall_bandwidth_per_sample_sum /
      MAX2(cmd->state.rp.drawcall_count, 1);

   struct u_trace_address addr = {};
   if (cmd->state.lrz.image_view) {
      struct tu_image *image = cmd->state.lrz.image_view->image;
      addr.bo = image->mem->bo;
      addr.offset = (image->iova - image->mem->iova) +
                    image->lrz_layout.lrz_fc_offset +
                    offsetof(fd_lrzfc_layout<CHIP>, buffer[0].dir_track);
   }

   int32_t lrz_disabled_at_draw = cmd->state.rp.lrz_disabled_at_draw
                                     ? cmd->state.rp.lrz_disabled_at_draw
                                     : -1;
   int32_t lrz_write_disabled_at_draw =
      cmd->state.rp.lrz_write_disabled_at_draw
         ? cmd->state.rp.lrz_write_disabled_at_draw
         : -1;
   trace_end_render_pass(
      &cmd->trace, &cmd->cs, gmem,
      cmd->state.rp.gmem_disable_reason ? cmd->state.rp.gmem_disable_reason
                                        : "",
      cmd->state.rp.drawcall_count, avg_per_sample_bandwidth,
      cmd->state.lrz.valid,
      cmd->state.rp.lrz_disable_reason ? cmd->state.rp.lrz_disable_reason
                                       : "",
      lrz_disabled_at_draw, lrz_write_disabled_at_draw, addr);
}

static void
tu_emit_renderpass_begin(struct tu_cmd_buffer *cmd)
{
   /* We need to re-emit any draw states that are patched in order for them to
    * be correctly added to the per-renderpass patchpoint list, even if they
    * are the same as before.
    */
   if (cmd->state.pass->has_fdm)
      cmd->state.dirty |= TU_CMD_DIRTY_FDM;

   /* We need to re-emit MSAA at the beginning of every renderpass because it
    * isn't part of a draw state that gets automatically re-emitted.
    */
   BITSET_SET(cmd->vk.dynamic_graphics_state.dirty,
              MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES);
   /* PC_CNTL isn't a part of a draw state and may be changed
    * by blits.
    */
   BITSET_SET(cmd->vk.dynamic_graphics_state.dirty,
              MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE);

   cmd->state.fdm_enabled = cmd->state.pass->has_fdm;
}

static inline bool
tu7_cb_disable_reason(bool disable_cb,
                      struct tu_cmd_buffer *cmd,
                      const char *reason)
{
   if (disable_cb && !cmd->state.rp.cb_disable_reason) {
      cmd->state.rp.cb_disable_reason = reason;
   }
   return disable_cb;
}

static bool
tu7_emit_concurrent_binning_start(struct tu_cmd_buffer *cmd,
                                  struct tu_cs *cs,
                                  bool disable_cb)
{
   if (tu7_cb_disable_reason(disable_cb, cmd, "disable_cb") ||
       /* LRZ can only be cleared via fast clear in BV. Disable CB if we can't
        * use it.
        */
       tu7_cb_disable_reason(
          (!cmd->state.lrz.fast_clear && cmd->state.lrz.image_view), cmd,
          "LRZ fast clear disabled") ||
       tu7_cb_disable_reason(TU_DEBUG(NO_CONCURRENT_BINNING), cmd,
                             "TU_DEBUG(NO_CONCURRENT_BINNING)")) {
     tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
     tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR) |
                    CP_THREAD_CONTROL_0_CONCURRENT_BIN_DISABLE);
     tu7_set_pred_bit(cs, TU_PREDICATE_CB_ENABLED, false);
     cmd->state.renderpass_cb_disabled = true;

     tu_add_cb_barrier_info(cmd);

     return false;
  }

  return true;
}

static void
tu7_emit_concurrent_binning(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   assert(!cmd->state.renderpass_cb_disabled);

   tu7_thread_control(cs, CP_SET_THREAD_BOTH);

   /* Increment timestamp to make it unique in subsequent commands */
   tu_cs_emit_pkt7(cs, CP_MODIFY_TIMESTAMP, 1);
   tu_cs_emit(cs, CP_MODIFY_TIMESTAMP_0_ADD(1) |
                  CP_MODIFY_TIMESTAMP_0_OP(MODIFY_TIMESTAMP_ADD_LOCAL));

   /* We initialize the "is concurrent binning enabled?" predicate to true and
    * disable it later if necessary.
    */
   tu7_set_pred_bit(cs, TU_PREDICATE_CB_ENABLED, true);

   tu7_thread_control(cs, CP_SET_THREAD_BV);

   /* If there was an overflow in the BR resource table the register will be
    * set to 1 by CP_RESOURCE_LIST. Wait for it to clear here.
    */
   tu7_wait_onchip_val(cs, TU_ONCHIP_CB_RESLIST_OVERFLOW, 0);

   tu_lrz_cb_begin(cmd, cs);
}

template <chip CHIP>
static void
tu7_emit_concurrent_binning_sysmem(struct tu_cmd_buffer *cmd,
                                   struct tu_cs *cs)
{
   /* Why all the complexity?
    * The logic necessary to support concurrent binning running in parallel to
    * sysmem has enough overhead to reduce performance for a workload with
    * high number of renderpasses, so we have to patch out the CB logic if
    * CB cannot run in parallel to this renderpass.
    * It does everything in IB1 because from testing the CB logic hangs in IB2.
    */

   struct tu_cs_patchable_state cb_state = tu_cs_patchable_start(cs, 128);

   /* It seems that for sysmem render passes we have to use BV to clear LRZ
    * before the renderpass. Otherwise the clear doesn't become visible to
    * subsequent draws when LRZ has been flipped an odd number of times.
    * Presumably this works if concurrent binning is disabled, because the
    * blob relies on this, but that requires synchronizing BR and BV
    * unnecessarily, and we want BV to skip ahead across sysmem renderpasses.
    *
    * In the future, we may also support writing LRZ in BV.
    */
   {
      tu7_emit_concurrent_binning(cmd, cs);

      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BIN_VISIBILITY));

      tu_lrz_sysmem_begin<CHIP>(cmd, cs);

      tu_lrz_after_bv<CHIP>(cmd, cs);

      tu7_write_onchip_timestamp(cs, TU_ONCHIP_CB_BV_TIMESTAMP);

      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM7_BIN_VISIBILITY_END));

      tu7_thread_control(cs, CP_SET_THREAD_BR);

      tu7_wait_onchip_timestamp(cs, TU_ONCHIP_CB_BV_TIMESTAMP);

      tu_lrz_before_sysmem_br<CHIP>(cmd, cs);
   }
   tu_cs_patchable_end(cs, false, &cb_state);

   struct tu_cs_patchable_state no_cb_state = tu_cs_patchable_start(cs, 64);
   {
      tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
      tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR) |
                        CP_THREAD_CONTROL_0_CONCURRENT_BIN_DISABLE);
      tu7_set_pred_bit(cs, TU_PREDICATE_CB_ENABLED, false);
      tu_lrz_sysmem_begin<CHIP>(cmd, cs);
   }
   tu_cs_patchable_end(cs, true, &no_cb_state);

   struct tu_cb_control_point enable_cb_patch = {
      .type = TU_CB_CONTROL_TYPE_PATCHPOINT,
      .patchpoint = cb_state.nop_header,
      .patch_value = cb_state.enable_patch,
      .original_value = cb_state.disable_patch,
   };
   util_dynarray_append(&cmd->cb_control_points, enable_cb_patch);

   struct tu_cb_control_point disable_no_cb_patch = {
      .type = TU_CB_CONTROL_TYPE_PATCHPOINT,
      .patchpoint = no_cb_state.nop_header,
      .patch_value = no_cb_state.disable_patch,
      .original_value = no_cb_state.enable_patch,
   };
   util_dynarray_append(&cmd->cb_control_points, disable_no_cb_patch);
}

template <chip CHIP>
static void
tu6_sysmem_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                        struct tu_renderpass_result *autotune_result)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   if (CHIP == A6XX) {
      tu_lrz_sysmem_begin<CHIP>(cmd, cs);
   } else {
      if (tu7_emit_concurrent_binning_start(cmd, cs, false)) {
         tu7_emit_concurrent_binning_sysmem<CHIP>(cmd, cs);
      } else {
         tu_lrz_sysmem_begin<CHIP>(cmd, cs);
      }
   }

   assert(fb->width > 0 && fb->height > 0);
   tu6_emit_window_scissor<CHIP>(cs, 0, 0, fb->width - 1, fb->height - 1);
   tu6_emit_window_offset<CHIP>(cs, 0, 0);

   tu6_emit_bin_size<CHIP>(cs, 0, 0, {
      .render_mode = RENDERING_PASS,
      .force_lrz_write_dis =
         !cmd->device->physical_device->info->props.has_lrz_feedback,
      .buffers_location = BUFFERS_IN_SYSMEM,
      .lrz_feedback_zmode_mask =
         cmd->device->physical_device->info->props.has_lrz_feedback
            ? LRZ_FEEDBACK_EARLY_Z_OR_EARLY_Z_LATE_Z
            : LRZ_FEEDBACK_NONE,
   });

   if (CHIP == A7XX) {
      tu7_emit_sysmem_render_begin_regs<CHIP>(cmd, cs);
   }

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_DIRECT_RENDER));

   /* A7XX TODO: blob doesn't use CP_SKIP_IB2_ENABLE_* */
   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_emit_cache_flush_ccu<CHIP>(cmd, cs, TU_CMD_CCU_SYSMEM);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);

   /* Reset bin scaling. */
   if (cmd->device->physical_device->info->props.has_hw_bin_scaling) {
      tu_cs_emit_regs(cs, GRAS_BIN_FOVEAT(CHIP));
      tu_cs_emit_regs(cs, RB_BIN_FOVEAT(CHIP));
   }

   tu_autotune_begin_renderpass<CHIP>(cmd, cs, autotune_result);

   tu_cs_sanity_check(cs);
}

template <chip CHIP>
static void
tu6_sysmem_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      struct tu_renderpass_result *autotune_result)
{
   tu_autotune_end_renderpass<CHIP>(cmd, cs, autotune_result);

   /* Do any resolves of the last subpass. These are handled in the
    * tile_store_cs in the gmem path.
    */
   tu6_emit_sysmem_resolves<CHIP>(cmd, cs, cmd->state.subpass);

   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_lrz_sysmem_end<CHIP>(cmd, cs);

   /* Clear the resource list for any LRZ resources we emitted at the
    * beginning.
    */
   if (CHIP >= A7XX) {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE7_0_EVENT(DUMMY_EVENT) |
                     CP_EVENT_WRITE7_0_CLEAR_RENDER_RESOURCE |
                     CP_EVENT_WRITE7_0_WRITE_DST(EV_DST_ONCHIP) |
                     CP_EVENT_WRITE7_0_WRITE_SRC(EV_WRITE_USER_32B) |
                     CP_EVENT_WRITE7_0_WRITE_ENABLED);
      tu_cs_emit_qw(cs, TU_ONCHIP_CB_RESLIST_OVERFLOW);
      tu_cs_emit(cs, 0); /* value */
   }

   tu_cs_sanity_check(cs);
}

static void
tu7_write_and_wait_onchip_timestamp(struct tu_cs *cs, enum tu_onchip_addr onchip_addr)
{
   tu7_write_onchip_timestamp(cs, onchip_addr);
   tu7_wait_onchip_timestamp(cs, onchip_addr);
}

static bool
tu7_emit_concurrent_binning_gmem(struct tu_cmd_buffer *cmd, struct tu_cs *cs, 
                                 bool use_hw_binning)
{
   /* xfb queries use data from the binning pass. If they are running outside
    * of a RP then we may have to deal with a mix of GMEM/sysmem renderpasses
    * where the counters increase on different processors. Just disable CB so
    * that everything happens on BR and we don't need difficult merging of BV
    * and BR results. In addition, RBBM primitive counters seem to not work
    * at all with concurrent binning, so disable if they are running before
    * the RP.
    */
   bool disable_cb =
      cmd->state.xfb_query_running_before_rp ||
      cmd->state.rp.has_prim_generated_query_in_rp ||
      cmd->state.rp.has_vtx_stats_query_in_rp ||
      cmd->state.prim_counters_running > 0;

   tu7_cb_disable_reason(disable_cb, cmd,
      "xfb/prim-gen/prim-counters/vtx-stats query is running");
   tu7_cb_disable_reason(!use_hw_binning, cmd, "hw binning disabled");

   if (!tu7_emit_concurrent_binning_start(cmd, cs, disable_cb || !use_hw_binning))
      return false;

   tu7_emit_concurrent_binning(cmd, cs);

   struct tu_cb_control_point cb_enabled_info = {
      .type = TU_CB_CONTROL_TYPE_CB_ENABLED,
   };
   util_dynarray_append(&cmd->cb_control_points, cb_enabled_info);

   /* We want to disable concurrent binning if BV isn't far enough ahead of
    * BR. The core idea is to write a timestamp in BR and BV, and compare the
    * BR and BV timestamps for equality. if BR is fast enough, it will write
    * the timestamp ahead of BV and then when BV compares for equality it will
    * find them equal. BR cannot race too far ahead of BV because it must wait
    * for BV's determination to finish, which we do via another timestamp, so
    * either BV is ahead of BR or the timestamps are equal.
    *
    * We need to communicate the determination from BV to BR so they both
    * agree on whether concurrent binning is enabled or not. The easiest way
    * to do it is via a "when was concurrent binning last disabled" timestamp,
    * because we only have to set it when disabling concurrent binning.
    */

   if (!TU_DEBUG(FORCE_CONCURRENT_BINNING)) {
      tu7_write_and_wait_onchip_timestamp(cs, TU_ONCHIP_CB_BV_TIMESTAMP);
      
      tu7_thread_control(cs, CP_SET_THREAD_BR);
      tu7_write_and_wait_onchip_timestamp(cs, TU_ONCHIP_CB_BR_TIMESTAMP);

      tu7_thread_control(cs, CP_SET_THREAD_BV);

      /* If in a secondary, dynamically disable CB if a vtx stats query is
       * running.
       */
      if (cmd->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
         tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(PRED_TEST) |
                                CP_COND_REG_EXEC_0_PRED_BIT(TU_PREDICATE_VTX_STATS_RUNNING));
      }

      const uint32_t bv_cond_dwords = 3 + 4 + 4;
      tu_cs_reserve(cs, 4 + bv_cond_dwords);

      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 3);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(REG_COMPARE) |
                     CP_COND_REG_EXEC_0_REG0(TU_ONCHIP_CB_BR_TIMESTAMP) |
                     CP_COND_REG_EXEC_0_ONCHIP_MEM);
      tu_cs_emit(cs, REG_COMPARE_CP_COND_REG_EXEC_1_REG1(TU_ONCHIP_CB_BV_TIMESTAMP) |
                     REG_COMPARE_CP_COND_REG_EXEC_1_ONCHIP_MEM);
      tu_cs_emit(cs, bv_cond_dwords);
      if (cmd->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
         tu_cond_exec_end(cs);
      /* if (BR_TIMESTAMP == BV_TIMESTAMP) */ {
         tu7_write_and_wait_onchip_timestamp(cs, TU_ONCHIP_CB_BV_DISABLED_TIMESTAMP);
         tu7_set_pred_bit(cs, TU_PREDICATE_CB_ENABLED, false);
      }
      tu7_write_onchip_timestamp(cs,
                                 TU_ONCHIP_CB_BV_DETERMINATION_FINISHED_TIMESTAMP);

      tu7_thread_control(cs, CP_SET_THREAD_BR);

      tu7_wait_onchip_timestamp(cs, TU_ONCHIP_CB_BV_DETERMINATION_FINISHED_TIMESTAMP);

      const uint32_t br_cond_dwords = 4;
      tu_cs_reserve(cs, 4 + br_cond_dwords);

      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 3);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(REG_COMPARE) |
                     CP_COND_REG_EXEC_0_REG0(TU_ONCHIP_CB_BR_TIMESTAMP) |
                     CP_COND_REG_EXEC_0_ONCHIP_MEM);
      tu_cs_emit(cs, REG_COMPARE_CP_COND_REG_EXEC_1_REG1(TU_ONCHIP_CB_BV_DISABLED_TIMESTAMP) |
                     REG_COMPARE_CP_COND_REG_EXEC_1_ONCHIP_MEM);
      tu_cs_emit(cs, br_cond_dwords);
      /* if (BR_TIMESTAMP == BV_DISABLED_TIMESTAMP) */ {
         tu7_set_pred_bit(cs, TU_PREDICATE_CB_ENABLED, false);
      }
   }

   /* At this point BV and BR are agreed on whether CB is enabled. If CB is
    * enabled, set the thread to BV for the binning pass, otherwise set BR and
    * disable concurrent binning.
    */
   tu7_thread_control(cs, CP_SET_THREAD_BOTH);

   const uint32_t if_dwords = 5;
   const uint32_t else_dwords = 2;
   tu_cs_reserve(cs, 3 + if_dwords + else_dwords);

   tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
   tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(PRED_TEST) |
                  CP_COND_REG_EXEC_0_PRED_BIT(TU_PREDICATE_CB_ENABLED) |
                  CP_COND_REG_EXEC_0_SKIP_WAIT_FOR_ME);
   tu_cs_emit(cs, if_dwords);
   /* if (CB is enabled) */ {
      tu7_thread_control(cs, CP_SET_THREAD_BV);

      /* Wait for BR vis stream reads to finish */
      tu_cs_emit_pkt7(cs, CP_BV_BR_COUNT_OPS, 1);
      tu_cs_emit(cs, CP_BV_BR_COUNT_OPS_0_OP(PIPE_BV_WAIT_FOR_BR));

      /* This is the NOP-as-else trick. If CB is disabled, this CP_NOP is
       * skipped and its body (the else) is executed.
       */
      tu_cs_emit_pkt7(cs, CP_NOP, else_dwords);
   } /* else */ {
      tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
      tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR) |
                     CP_THREAD_CONTROL_0_CONCURRENT_BIN_DISABLE);
   }

   return true;
}

template <chip CHIP>
static void
tu6_tile_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      struct tu_renderpass_result *autotune_result,
                      const VkOffset2D *fdm_offsets)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, tiling);
   const struct tu_render_pass *pass = cmd->state.pass;
   bool use_binning = use_hw_binning(cmd);

   /* User flushes should always be executed on BR. */
   tu_emit_cache_flush_ccu<CHIP>(cmd, cs, TU_CMD_CCU_GMEM);

   bool use_cb = false;

   if (CHIP >= A7XX) {
      tu7_emit_tile_render_begin_regs<CHIP>(cs);
      use_cb = tu7_emit_concurrent_binning_gmem(cmd, cs, use_binning);
   }

   if (!use_cb)
      tu_trace_start_render_pass(cmd);

   tu_lrz_tiling_begin<CHIP>(cmd, cs);

   /* tu_lrz_tiling_begin() can accumulate additional flushes. If that happens
    * CB should be disabled, so it's safe to just emit them here.
    */
   tu_emit_cache_flush_ccu<CHIP>(cmd, cs, TU_CMD_CCU_GMEM);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   /* Reset bin scaling. */
   if (phys_dev->info->props.has_hw_bin_scaling) {
      tu_cs_emit_regs(cs, GRAS_BIN_FOVEAT(CHIP));
      tu_cs_emit_regs(cs, RB_BIN_FOVEAT(CHIP));
   }

   if (use_binning) {
      if (!cmd->vsc_initialized) {
         tu6_lazy_init_vsc(cmd);
      }

      /* We always emit VSC before each renderpass, because due to
       * skipsaverestore the underlying VSC registers may have become
       * invalid. Normally we'd need to WFI before setting these non-context
       * registers, but we should be safe because we're only setting it to the
       * same value it had before.
       *
       * TODO: On a6xx, we have to emit this per-bin or make the amble include
       * these registers, because CP_SET_BIN_DATA5_OFFSET will use the
       * register instead of the pseudo register and its value won't survive
       * across preemptions. The blob seems to take the second approach and
       * emits the preamble lazily. We chose the per-bin approach but blob's
       * should be a better one.
       */
       if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
         tu_cs_set_writeable(cs, true);

      tu_emit_vsc<CHIP>(cmd, cs);

       if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
         tu_cs_set_writeable(cs, false);

      tu6_emit_bin_size<CHIP>(cs, tiling->tile0.width, tiling->tile0.height,
                              {
                                 .render_mode = BINNING_PASS,
                                 .buffers_location = BUFFERS_IN_GMEM,
                                 .lrz_feedback_zmode_mask =
                                    phys_dev->info->props.has_lrz_feedback
                                       ? LRZ_FEEDBACK_EARLY_Z_LATE_Z
                                       : LRZ_FEEDBACK_NONE
                              });

      tu6_emit_render_cntl<CHIP>(cmd, cmd->state.subpass, cs, true);

      tu6_emit_binning_pass<CHIP>(cmd, cs, fdm_offsets, use_cb);

      /* Enable early return from CP_INDIRECT_BUFFER once the visibility stream
       * is done.  We don't enable this if there are stores in a non-final
       * subpass, because it's more important to be able to share gmem space
       * between attachments by storing early, than it is to do IB2 skipping
       * (which has an effect we struggle to even measure).
       */
      if (pass->allow_ib2_skipping) {
         tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
         tu_cs_emit(cs, 0x1);
         tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_LOCAL, 1);
         tu_cs_emit(cs, 0x1);
      }
   } else {
      if (vsc->binning_possible) {
         /* Mark all tiles as visible for tu6_emit_cond_for_load_stores(), since
          * the actual binner didn't run.
          */
         int pipe_count = vsc->pipe_count.width * vsc->pipe_count.height;
         tu_cs_emit_pkt4(cs, REG_A6XX_VSC_CHANNEL_VISIBILITY(0), pipe_count);
         for (int i = 0; i < pipe_count; i++)
            tu_cs_emit(cs, ~0);
      }
   }

   if (vsc->binning_possible) {
      /* On a7xx we always need VSC allocated because the VSC state has to go
       * together with other stream data. We could allocate just the VSC state
       * if binning is disabled but it doesn't seem worth it.
       */
      if (CHIP >= A7XX && !cmd->vsc_initialized)
         tu6_lazy_init_vsc(cmd);

      /* Upload state regs to memory to be restored on skipsaverestore
       * preemption. On a7xx this is considered part of the vis stream that
       * requires a patchpoint.
       */
      if (CHIP >= A7XX &&
          (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
         tu_cs_set_writeable(cs, true);

      tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
      tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_VSC_CHANNEL_VISIBILITY(0)) |
                     CP_REG_TO_MEM_0_CNT(32));
      if (CHIP >= A7XX)
         tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_state_offset);
      else
         tu_cs_emit_qw(cs, global_iova(cmd, vsc_state));

      if (CHIP >= A7XX) {
         uint32_t num_vsc_pipes = phys_dev->info->num_vsc_pipes;

         tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
         tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

         if (use_binning) {
            tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
            tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BV));

            tu_lrz_after_bv<CHIP>(cmd, cs);

            /* Signal that BV is done for this render pass. This always has to
             * be executed, even when CB is dynamically disabled, because we
             * need to keep BR and BV counts in sync with which visibility
             * streams are in use.
             */
            tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 1);
            tu_cs_emit(cs, CP_EVENT_WRITE7_0_EVENT(DUMMY_EVENT) |
                           CP_EVENT_WRITE7_0_INC_BV_COUNT);

            /* This mode seems to be only used by BV and signals that a
             * simpler save/restore procedure can be used in between render
             * passes.
             */
            tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
            tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM7_BIN_VISIBILITY_END));
         }

         tu7_thread_control(cs, CP_SET_THREAD_BR);

         if (use_binning) {
            /* Wait for the BV to be done for this render pass. */
            tu_cs_emit_pkt7(cs, CP_BV_BR_COUNT_OPS, 1);
            tu_cs_emit(cs, CP_BV_BR_COUNT_OPS_0_OP(PIPE_BR_WAIT_FOR_BV));

            /* Emit vis stream on BR */
            tu_emit_vsc<CHIP>(cmd, cs);
         }

         tu_cs_emit_pkt7(cs, CP_MEM_TO_SCRATCH_MEM, 4);
         tu_cs_emit(cs, num_vsc_pipes); /* count */
         tu_cs_emit(cs, 0); /* offset */
         tu_emit_vis_stream_patchpoint(cmd, cs, cmd->vsc_state_offset);
      }

      if (CHIP >= A7XX &&
          (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
         tu_cs_set_writeable(cs, false);
   } else if (CHIP >= A7XX) {
      /* Earlier we disabled concurrent binning to make LRZ fast-clear work
       * with no HW binning, now re-enable it while staying on BR.
       */
      tu7_set_thread_br_patchpoint(cmd, cs, false);
   }

   tu_lrz_before_tiles<CHIP>(cmd, cs, use_cb);

   if (use_cb)
      tu_trace_start_render_pass(cmd);

   tu_autotune_begin_renderpass<CHIP>(cmd, cs, autotune_result);

   tu_cs_sanity_check(cs);
}

template <chip CHIP>
static void
tu6_render_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                const struct tu_tile_config *tile,
                bool fdm, const VkOffset2D *fdm_offsets)
{
   tu6_emit_tile_select<CHIP>(cmd, &cmd->cs, tile, fdm, fdm_offsets);
   tu_lrz_before_tile<CHIP>(cmd, &cmd->cs);

   trace_start_draw_ib_gmem(&cmd->trace, &cmd->cs, cmd);

   /* Primitives that passed all tests are still counted in in each
    * tile even with HW binning beforehand. Do not permit it.
    */
   if (cmd->state.prim_generated_query_running_before_rp)
      tu_emit_event_write<CHIP>(cmd, cs, FD_STOP_PRIMITIVE_CTRS);

   tu_cs_emit_call(cs, &cmd->draw_cs);

   if (cmd->state.prim_generated_query_running_before_rp)
      tu_emit_event_write<CHIP>(cmd, cs, FD_START_PRIMITIVE_CTRS);

   if (use_hw_binning(cmd)) {
      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BIN_END_OF_DRAWS) |
                     A6XX_CP_SET_MARKER_0_USES_GMEM);
   }

   /* Predicate is changed in draw_cs so we have to re-emit it */
   if (cmd->state.rp.draw_cs_writes_to_cond_pred &&
       util_is_power_of_two_nonzero(tile->slot_mask)) {
      uint32_t slot = ffs(tile->slot_mask) - 1;
      tu6_emit_cond_for_load_stores<CHIP>(cmd, cs, tile->pipe, slot, false);
   }

   if (cmd->state.pass->allow_ib2_skipping) {
      /* Disable CP_INDIRECT_BUFFER/CP_DRAW skipping again at the end of the
       * pass -- tile_store_cs is for stores that can't be skipped based on
       * visibility.
       */
      tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
      tu_cs_emit(cs, 0x0);
   }

   tu_cs_emit_call(cs, &cmd->tile_store_cs);

   tu_clone_trace_range(cmd, cs, &cmd->trace, cmd->trace_renderpass_start,
                        u_trace_end_iterator(&cmd->rp_trace));
   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BIN_RENDER_END));

   tu_cs_sanity_check(cs);

   trace_end_draw_ib_gmem(&cmd->trace, &cmd->cs);
}

template <chip CHIP>
static void
tu6_tile_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                    struct tu_renderpass_result *autotune_result)
{
   tu_autotune_end_renderpass<CHIP>(cmd, cs, autotune_result);

   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_lrz_tiling_end<CHIP>(cmd, cs);

   bool hw_binning = use_hw_binning(cmd);
   if (hw_binning) {
      cmd->state.tile_render_pass_count++;
   }

   /* If we are using HW binning, signal that we are done with reading the vis
    * stream for this render pass by advancing the counter. Also clear render
    * resources, currently only used for LRZ, and reset the overflow onchip
    * register.
    */
   if (CHIP >= A7XX) {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE7_0_EVENT(DUMMY_EVENT) |
                     COND(hw_binning, CP_EVENT_WRITE7_0_INC_BR_COUNT) |
                     CP_EVENT_WRITE7_0_CLEAR_RENDER_RESOURCE |
                     CP_EVENT_WRITE7_0_WRITE_DST(EV_DST_ONCHIP) |
                     CP_EVENT_WRITE7_0_WRITE_SRC(EV_WRITE_USER_32B) |
                     CP_EVENT_WRITE7_0_WRITE_ENABLED);
      tu_cs_emit_qw(cs, TU_ONCHIP_CB_RESLIST_OVERFLOW);
      tu_cs_emit(cs, 0); /* value */
   }

   tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_BLIT_CACHE);

   tu_cs_sanity_check(cs);
}

static void
tu_calc_frag_area(struct tu_cmd_buffer *cmd,
                  struct tu_tile_config *tile,
                  const struct tu_image_view *fdm,
                  const VkOffset2D *fdm_offsets)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const uint32_t x1 = tiling->tile0.width * tile->pos.x;
   const uint32_t y1 = tiling->tile0.height * tile->pos.y;
   const uint32_t x2 = MIN2(x1 + tiling->tile0.width, MAX_VIEWPORT_SIZE);
   const uint32_t y2 = MIN2(y1 + tiling->tile0.height, MAX_VIEWPORT_SIZE);

   unsigned views = tu_fdm_num_layers(cmd);
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   struct tu_frag_area raw_areas[views];
   if (fdm) {
      for (unsigned i = 0; i < views; i++) {
         VkOffset2D sample_pos = { 0, 0 };

         /* Offsets less than a tile size are accomplished by sliding the
          * tiles.  However once we shift a whole tile size then we reset the
          * tiles back to where they were at the beginning and we need to
          * adjust where each bin is sampling from:
          *
          * x offset = 0:
          *
          * ------------------------------------
          * |   *   |   *   |   *   | (unused) |
          * ------------------------------------
          *
          * x offset = 4:
          *
          * -------------------------
          * | * |   *   |   *   | * |
          * -------------------------
          *
          * x offset = 8:
          *
          * ------------------------------------
          * |   *   |   *   |   *   | (unused) |
          * ------------------------------------
          *
          * As the user's offset increases we slide the tiles to the right,
          * until we reach the whole tile size and reset the tile positions.
          * tu_bin_offset() returns an amount to shift to the left, negating
          * the offset.
          *
          * If we were forced to use a shared viewport, then we must not shift
          * over the tiles and instead must only shift when sampling because
          * we cannot shift the tiles differently per view. This disables
          * smooth transitions of the fragment density map and effectively
          * negates the extension.
          *
          * Note that we cannot clamp x2/y2 to the framebuffer size, as we
          * normally would do, because then tiles along the edge would
          * incorrectly nudge the sample_pos towards the center of the
          * framebuffer. If we shift one complete tile over towards the
          * center and reset the tiles as above, the sample_pos would
          * then shift back towards the edge and we could get a "pop" from
          * suddenly changing density due to the slight shift.
          */
         if (fdm_offsets) {
            VkOffset2D offset = fdm_offsets[i];
            if (!cmd->state.rp.shared_viewport) {
               VkOffset2D bin_offset = tu_bin_offset(fdm_offsets[i], tiling);
               offset.x += bin_offset.x;
               offset.y += bin_offset.y;
            }
            sample_pos.x = (x1 + x2) / 2 - offset.x;
            sample_pos.y = (y1 + y2) / 2 - offset.y;
         } else {
            sample_pos.x = (x1 + MIN2(x2, fb->width)) / 2;
            sample_pos.y = (y1 + MIN2(y2, fb->height)) / 2;
         }

         tu_fragment_density_map_sample(fdm,
                                        sample_pos.x,
                                        sample_pos.y,
                                        fb->width, fb->height, i,
                                        &raw_areas[i]);
      }
   } else {
      for (unsigned i = 0; i < views; i++)
         raw_areas[i].width = raw_areas[i].height = 1.0f;
   }

   for (unsigned i = 0; i < views; i++) {
      float floor_x, floor_y;
      float area = raw_areas[i].width * raw_areas[i].height;
      float frac_x = modff(raw_areas[i].width, &floor_x);
      float frac_y = modff(raw_areas[i].height, &floor_y);
      /* The spec allows rounding up one of the axes as long as the total
       * area is less than or equal to the original area. Take advantage of
       * this to try rounding up the number with the largest fraction.
       */
      if ((frac_x > frac_y ? (floor_x + 1.f) * floor_y :
                              floor_x * (floor_y + 1.f)) <= area) {
         if (frac_x > frac_y)
            floor_x += 1.f;
         else
            floor_y += 1.f;
      }
      uint32_t width = floor_x;
      uint32_t height = floor_y;

      /* Areas that aren't a power of two, especially large areas, can create
       * in floating-point rounding errors when dividing by the area in the
       * viewport that result in under-rendering. Round down to a power of two
       * to make sure all operations are exact.
       */
      width = 1u << util_logbase2(width);
      height = 1u << util_logbase2(height);

      /* When FDM offset is enabled, the fragment area has to divide the
       * offset to make sure that we don't have tiles with partial fragments.
       * It would be bad to have the fragment area change as a function of the
       * offset, because we'd get "popping" as the resolution changes with the
       * offset, so just make sure it divides the offset granularity. This
       * should mean it always divides the offset for any possible offset.
       */
      if (fdm_offsets) {
         width = MIN2(width, TU_FDM_OFFSET_GRANULARITY);
         height = MIN2(height, TU_FDM_OFFSET_GRANULARITY);
      }

      /* HW viewport scaling supports a maximum fragment width/height of 4.
       */
      if (views <= MAX_HW_SCALED_VIEWS) {
         width = MIN2(width, 4);
         height = MIN2(height, 4);
      }

      /* Make sure that the width/height divides the tile width/height so
       * we don't have to do extra awkward clamping of the edges of each
       * bin when resolving. It also has to divide the fdm offset, if any.
       * Note that because the tile width is rounded to a multiple of 32 any
       * power of two 32 or less will work, and if there is an offset then it
       * must be a multiple of 4 so 2 or 4 will definitely work.
       *
       * TODO: Try to take advantage of the total area allowance here, too.
       */
      while (tiling->tile0.width % width != 0)
         width /= 2;
      while (tiling->tile0.height % height != 0)
         height /= 2;

      tile->frag_areas[i].width = width;
      tile->frag_areas[i].height = height;
   }

   /* If at any point we were forced to use the same scaling for all
    * viewports, we need to make sure that any users *not* using shared
    * scaling, including loads/stores, also consistently share the scaling. 
    */
   if (cmd->state.rp.shared_viewport) {
      VkExtent2D frag_area = { UINT32_MAX, UINT32_MAX };
      for (unsigned i = 0; i < views; i++) {
         frag_area.width = MIN2(frag_area.width, tile->frag_areas[i].width);
         frag_area.height = MIN2(frag_area.height, tile->frag_areas[i].height);
      }

      for (unsigned i = 0; i < views; i++)
         tile->frag_areas[i] = frag_area;
   }
}

static bool
try_merge_tiles(struct tu_tile_config *dst, const struct tu_tile_config *src,
                unsigned views, bool has_abs_bin_mask)
{
   uint32_t slot_mask = dst->slot_mask | src->slot_mask;

   /* The fragment areas must be the same. */
   for (unsigned i = 0; i < views; i++) {
      if (dst->frag_areas[i].width != src->frag_areas[i].width ||
          dst->frag_areas[i].height != src->frag_areas[i].height)
         return false;
   }

   /* The tiles must be vertically or horizontally adjacent and have the
    * compatible width/height.
    */
   if (dst->pos.x == src->pos.x) {
      if (dst->extent.height != src->extent.height)
         return false;
   } else if (dst->pos.y == src->pos.y) {
      if (dst->extent.width != src->extent.width)
         return false;
   } else {
      return false;
   }

   if (!has_abs_bin_mask) {
      /* The mask of the combined tile has to fit in 16 bits */
      uint32_t hw_mask = slot_mask >> (ffs(slot_mask) - 1);
      if ((hw_mask & 0xffff) != hw_mask)
         return false;
   }

   /* Note, this assumes that dst is below or to the right of src, which is
    * how we call this function below.
    */
   VkExtent2D extent = {
      dst->extent.width + (dst->pos.x - src->pos.x),
      dst->extent.height + (dst->pos.y - src->pos.y),
   };

   assert(dst->extent.height > 0);

   /* The common fragment areas must not be smaller than the combined bin
    * extent, so that the combined bin is not larger than the original
    * unscaled bin.
    */
   for (unsigned i = 0; i < views; i++) {
      if (dst->frag_areas[i].width < extent.width ||
          dst->frag_areas[i].height < extent.height)
         return false;
   }

   /* Ok, let's combine them. dst is below or to the right of src, so it takes
    * src's position.
    */
   dst->extent = extent;
   dst->pos = src->pos;
   dst->slot_mask = slot_mask;
   return true;
}

template <chip CHIP>
void
tu_render_pipe_fdm(struct tu_cmd_buffer *cmd, uint32_t pipe,
                   uint32_t tx1, uint32_t ty1, uint32_t tx2, uint32_t ty2,
                   const struct tu_image_view *fdm,
                   const VkOffset2D *fdm_offsets)
{
   uint32_t width = tx2 - tx1;
   uint32_t height = ty2 - ty1;
   unsigned views = tu_fdm_num_layers(cmd);
   bool has_abs_mask =
      cmd->device->physical_device->info->props.has_abs_bin_mask;

   struct tu_tile_config tiles[width * height];

   /* Initialize tiles and sample fragment density map */
   for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
         struct tu_tile_config *tile = &tiles[width * y + x];
         tile->pos = { x + tx1, y + ty1 };
         tile->extent = { 1, 1 };
         tile->pipe = pipe;
         tile->slot_mask = 1u << (width * y + x);
         tu_calc_frag_area(cmd, tile, fdm, fdm_offsets);
      }
   }

   uint32_t merged_tiles = 0;

   /* Merge tiles */
   for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
         struct tu_tile_config *tile = &tiles[width * y + x];
         if (x > 0) {
            struct tu_tile_config *prev_x_tile = &tiles[width * y + x - 1];
            if (try_merge_tiles(tile, prev_x_tile, views, has_abs_mask)) {
               merged_tiles |= prev_x_tile->slot_mask;
            }
         }
         if (y > 0) {
            unsigned prev_y_idx = width * (y - 1) + x;
            struct tu_tile_config *prev_y_tile = &tiles[prev_y_idx];

            /* We can't merge prev_y_tile into tile if it's already been
             * merged horizontally into its neighbor in the previous row.
             */
            if (!(merged_tiles & (1u << prev_y_idx)) &&
                try_merge_tiles(tile, prev_y_tile, views, has_abs_mask)) {
               merged_tiles |= prev_y_tile->slot_mask;
            }
         }
      }
   }

   /* Finally, iterate over tiles and draw them */
   for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
         uint32_t tx;
         if (y & 1)
            tx = width - 1 - x;
         else
            tx = x;

         unsigned tile_idx = y * width + tx;
         if (merged_tiles & (1u << tile_idx))
            continue;

         tu6_render_tile<CHIP>(cmd, &cmd->cs, &tiles[tile_idx],
                               true, fdm_offsets);
      }
   }
}

static VkResult
tu_allocate_transient_attachments(struct tu_cmd_buffer *cmd, bool sysmem)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_render_pass *rp = cmd->state.pass;

   for (unsigned i = 0; i < fb->attachment_count; i++) {
      const struct tu_image_view *iview = cmd->state.attachments[i];
      if (iview && !(iview->image->vk.create_flags &
                     VK_IMAGE_CREATE_SPARSE_BINDING_BIT) &&
          !iview->image->mem->bo &&
          (sysmem || rp->attachments[i].load ||
           rp->attachments[i].load_stencil ||
           rp->attachments[i].store ||
           rp->attachments[i].store_stencil)) {
         VkResult result = tu_allocate_lazy_memory(cmd->device,
                                                   iview->image->mem);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}

template <chip CHIP>
static void
tu_cmd_render_tiles(struct tu_cmd_buffer *cmd,
                    struct tu_renderpass_result *autotune_result,
                    const VkOffset2D *fdm_offsets)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, tiling);
   const struct tu_image_view *fdm = NULL;

   VkResult result = tu_allocate_transient_attachments(cmd, false);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   if (cmd->state.pass->fragment_density_map.attachment != VK_ATTACHMENT_UNUSED) {
      fdm = cmd->state.attachments[cmd->state.pass->fragment_density_map.attachment];
   }

   bool has_fdm = fdm || (TU_DEBUG(FDM) && cmd->state.pass->has_fdm);
   bool merge_tiles = has_fdm && !TU_DEBUG(NO_BIN_MERGING) &&
      cmd->device->physical_device->info->props.has_bin_mask;

   /* If not using FDM make sure not to accidentally apply the offsets */
   if (!has_fdm)
      fdm_offsets = NULL;

   /* Create gmem stores now (at EndRenderPass time)) because they needed to
    * know whether to allow their conditional execution, which was tied to a
    * state that was known only at the end of the renderpass.  They will be
    * called from tu6_render_tile().
    */
   tu_cs_begin(&cmd->tile_store_cs);
   tu6_emit_tile_store_cs<CHIP>(cmd, &cmd->tile_store_cs);
   tu_cs_end(&cmd->tile_store_cs);

   tu6_tile_render_begin<CHIP>(cmd, &cmd->cs, autotune_result, fdm_offsets);

   /* Note: we reverse the order of walking the pipes and tiles on every
    * other row, to improve texture cache locality compared to raster order.
    */
   for (uint32_t py = 0; py < vsc->pipe_count.height; py++) {
      uint32_t pipe_row = py * vsc->pipe_count.width;
      for (uint32_t pipe_row_i = 0; pipe_row_i < vsc->pipe_count.width; pipe_row_i++) {
         uint32_t px;
         if (py & 1)
            px = vsc->pipe_count.width - 1 - pipe_row_i;
         else
            px = pipe_row_i;
         uint32_t pipe = pipe_row + px;
         uint32_t tx1 = px * vsc->pipe0.width;
         uint32_t ty1 = py * vsc->pipe0.height;
         uint32_t tx2 = MIN2(tx1 + vsc->pipe0.width, vsc->tile_count.width);
         uint32_t ty2 = MIN2(ty1 + vsc->pipe0.height, vsc->tile_count.height);

         if (merge_tiles) {
            tu_render_pipe_fdm<CHIP>(cmd, pipe, tx1, ty1, tx2, ty2, fdm,
                                     fdm_offsets);
            continue;
         }

         uint32_t tile_row_stride = tx2 - tx1;
         uint32_t slot_row = 0;
         for (uint32_t ty = ty1; ty < ty2; ty++) {
            for (uint32_t tile_row_i = 0; tile_row_i < tile_row_stride; tile_row_i++) {
               uint32_t tx;
               if (ty & 1)
                  tx = tile_row_stride - 1 - tile_row_i;
               else
                  tx = tile_row_i;

               struct tu_tile_config tile = {
                  .pos = { tx1 + tx, ty },
                  .pipe = pipe,
                  .slot_mask = 1u << (slot_row + tx),
                  .extent = { 1, 1 },
               };
               if (has_fdm)
                  tu_calc_frag_area(cmd, &tile, fdm, fdm_offsets);

               tu6_render_tile<CHIP>(cmd, &cmd->cs, &tile, has_fdm,
                                     fdm_offsets);
            }
            slot_row += tile_row_stride;
         }
      }
   }

   tu6_tile_render_end<CHIP>(cmd, &cmd->cs, autotune_result);

   tu_trace_end_render_pass<CHIP>(cmd, true);

   /* We have trashed the dynamically-emitted viewport, scissor, and FS params
    * via the patchpoints, so we need to re-emit them if they are reused for a
    * later render pass.
    */
   if (cmd->state.pass->has_fdm)
      cmd->state.dirty |= TU_CMD_DIRTY_FDM;

   /* Reset the gmem store CS entry lists so that the next render pass
    * does its own stores.
    */
   tu_cs_discard_entries(&cmd->tile_store_cs);
}

template <chip CHIP>
static void
tu_cmd_render_sysmem(struct tu_cmd_buffer *cmd,
                     struct tu_renderpass_result *autotune_result)
{
   VkResult result = tu_allocate_transient_attachments(cmd, true);

   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_trace_start_render_pass(cmd);

   tu6_sysmem_render_begin<CHIP>(cmd, &cmd->cs, autotune_result);

   trace_start_draw_ib_sysmem(&cmd->trace, &cmd->cs, cmd);

   tu_cs_emit_call(&cmd->cs, &cmd->draw_cs);

   trace_end_draw_ib_sysmem(&cmd->trace, &cmd->cs);

   tu6_sysmem_render_end<CHIP>(cmd, &cmd->cs, autotune_result);

   tu_clone_trace_range(cmd, &cmd->cs, &cmd->trace,
                        cmd->trace_renderpass_start,
                        u_trace_end_iterator(&cmd->rp_trace));

   tu_trace_end_render_pass<CHIP>(cmd, false);
}

template <chip CHIP>
void
tu_cmd_render(struct tu_cmd_buffer *cmd_buffer,
              const VkOffset2D *fdm_offsets)
{
   if (cmd_buffer->state.rp.has_tess)
      tu6_lazy_emit_tessfactor_addr<CHIP>(cmd_buffer);

   struct tu_renderpass_result *autotune_result = NULL;
   if (use_sysmem_rendering(cmd_buffer, &autotune_result))
      tu_cmd_render_sysmem<CHIP>(cmd_buffer, autotune_result);
   else
      tu_cmd_render_tiles<CHIP>(cmd_buffer, autotune_result, fdm_offsets);

   /* Outside of renderpasses we assume all draw states are disabled. We do
    * this outside the draw CS for the normal case where 3d gmem stores aren't
    * used.
    */
   tu_disable_draw_states(cmd_buffer, &cmd_buffer->cs);

}

static void tu_reset_render_pass(struct tu_cmd_buffer *cmd_buffer)
{
   /* discard draw_cs and draw_epilogue_cs entries now that the tiles are
      rendered */
   tu_cs_discard_entries(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_discard_entries(&cmd_buffer->draw_epilogue_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->state.pass = NULL;
   cmd_buffer->state.subpass = NULL;
   cmd_buffer->state.framebuffer = NULL;
   cmd_buffer->state.attachments = NULL;
   cmd_buffer->state.clear_values = NULL;
   cmd_buffer->state.gmem_layout = TU_GMEM_LAYOUT_COUNT; /* invalid value to prevent looking up gmem offsets */
   cmd_buffer->state.renderpass_cb_disabled = false;
   memset(&cmd_buffer->state.rp, 0, sizeof(cmd_buffer->state.rp));

   /* LRZ is not valid next time we use it */
   cmd_buffer->state.lrz.valid = false;
   cmd_buffer->state.dirty |= TU_CMD_DIRTY_LRZ;

   /* Patchpoints have been executed */
   util_dynarray_clear(&cmd_buffer->fdm_bin_patchpoints);
   ralloc_free(cmd_buffer->patchpoints_ctx);
   cmd_buffer->patchpoints_ctx = NULL;

   /* Discard RP trace contents */
   u_trace_disable_event_range(cmd_buffer->trace_renderpass_start,
                               u_trace_end_iterator(&cmd_buffer->rp_trace));
   cmd_buffer->trace_renderpass_start =
      u_trace_end_iterator(&cmd_buffer->rp_trace);
}

static VkResult
tu_create_cmd_buffer(struct vk_command_pool *pool,
                     VkCommandBufferLevel level,
                     struct vk_command_buffer **cmd_buffer_out)
{
   struct tu_device *device =
      container_of(pool->base.device, struct tu_device, vk);
   struct tu_cmd_buffer *cmd_buffer;

   cmd_buffer = (struct tu_cmd_buffer *) vk_zalloc2(
      &device->vk.alloc, NULL, sizeof(*cmd_buffer), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (cmd_buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(pool, &cmd_buffer->vk,
                                            &tu_cmd_buffer_ops, level);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, NULL, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;

   u_trace_init(&cmd_buffer->trace, &device->trace_context);
   u_trace_init(&cmd_buffer->rp_trace, &device->trace_context);
   cmd_buffer->trace_renderpass_start =
      u_trace_begin_iterator(&cmd_buffer->rp_trace);
   list_inithead(&cmd_buffer->renderpass_autotune_results);

   if (TU_DEBUG_START(CHECK_CMD_BUFFER_STATUS)) {
      cmd_buffer->status_bo = tu_cmd_buffer_setup_status_tracking(device);
      if (cmd_buffer->status_bo == NULL) {
         mesa_logw("Failed creating cmd_buffer status_bo. "
                   "Won't track status for this cmd_buffer.");
      }
   }

   tu_cs_init(&cmd_buffer->cs, device, TU_CS_MODE_GROW, 4096, "cmd cs");
   tu_cs_init(&cmd_buffer->draw_cs, device, TU_CS_MODE_GROW, 4096, "draw cs");
   tu_cs_init(&cmd_buffer->tile_store_cs, device, TU_CS_MODE_GROW, 2048, "tile store cs");
   tu_cs_init(&cmd_buffer->draw_epilogue_cs, device, TU_CS_MODE_GROW, 4096, "draw epilogue cs");
   tu_cs_init(&cmd_buffer->sub_cs, device, TU_CS_MODE_SUB_STREAM, 2048, "draw sub cs");
   tu_cs_init(&cmd_buffer->pre_chain.draw_cs, device, TU_CS_MODE_GROW, 4096, "prechain draw cs");
   tu_cs_init(&cmd_buffer->pre_chain.draw_epilogue_cs, device, TU_CS_MODE_GROW, 4096, "prechain draw epiligoue cs");

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++)
      cmd_buffer->descriptors[i].push_set.base.type = VK_OBJECT_TYPE_DESCRIPTOR_SET;

   *cmd_buffer_out = &cmd_buffer->vk;

   return VK_SUCCESS;
}

static void
tu_cmd_buffer_destroy(struct vk_command_buffer *vk_cmd_buffer)
{
   struct tu_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct tu_cmd_buffer, vk);

   tu_cs_finish(&cmd_buffer->cs);
   tu_cs_finish(&cmd_buffer->draw_cs);
   tu_cs_finish(&cmd_buffer->tile_store_cs);
   tu_cs_finish(&cmd_buffer->draw_epilogue_cs);
   tu_cs_finish(&cmd_buffer->sub_cs);
   tu_cs_finish(&cmd_buffer->pre_chain.draw_cs);
   tu_cs_finish(&cmd_buffer->pre_chain.draw_epilogue_cs);

   if (TU_DEBUG_START(CHECK_CMD_BUFFER_STATUS)) {
      tu_cmd_buffer_status_check_idle(cmd_buffer);
      tu_bo_unmap(cmd_buffer->device, cmd_buffer->status_bo, false);
      tu_bo_finish(cmd_buffer->device, cmd_buffer->status_bo);
   }

   u_trace_fini(&cmd_buffer->trace);
   u_trace_fini(&cmd_buffer->rp_trace);

   tu_autotune_free_results(cmd_buffer->device, &cmd_buffer->renderpass_autotune_results);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++) {
      if (cmd_buffer->descriptors[i].push_set.layout)
         vk_descriptor_set_layout_unref(&cmd_buffer->device->vk,
                                        &cmd_buffer->descriptors[i].push_set.layout->vk);
      vk_free(&cmd_buffer->device->vk.alloc,
              cmd_buffer->descriptors[i].push_set.mapped_ptr);
   }

   util_dynarray_foreach (&cmd_buffer->msrtss_color_temporaries,
                          struct tu_device_memory *, mem) {
      tu_destroy_memory(cmd_buffer->device, *mem);
   }
   util_dynarray_fini(&cmd_buffer->msrtss_color_temporaries);
   util_dynarray_foreach (&cmd_buffer->msrtss_depth_temporaries,
                          struct tu_device_memory *, mem) {
      tu_destroy_memory(cmd_buffer->device, *mem);
   }
   util_dynarray_fini(&cmd_buffer->msrtss_depth_temporaries);

   ralloc_free(cmd_buffer->patchpoints_ctx);
   ralloc_free(cmd_buffer->pre_chain.patchpoints_ctx);
   util_dynarray_fini(&cmd_buffer->fdm_bin_patchpoints);
   util_dynarray_fini(&cmd_buffer->pre_chain.fdm_bin_patchpoints);
   util_dynarray_fini(&cmd_buffer->vis_stream_patchpoints);
   util_dynarray_fini(&cmd_buffer->cb_control_points);

   util_dynarray_foreach (&cmd_buffer->vis_stream_bos, struct tu_bo *,
                          bo) {
      tu_bo_finish(cmd_buffer->device, *bo);
   }

   mtx_lock(&cmd_buffer->device->vis_stream_suballocator_mtx);
   util_dynarray_foreach (&cmd_buffer->vis_stream_cs_bos,
                          struct tu_vis_stream_patchpoint_cs,
                          bo) {
      tu_suballoc_bo_free(&cmd_buffer->device->vis_stream_suballocator,
                          &bo->cs_bo);
      tu_suballoc_bo_free(&cmd_buffer->device->vis_stream_suballocator,
                          &bo->fence_bo);
   }
   mtx_unlock(&cmd_buffer->device->vis_stream_suballocator_mtx);

   util_dynarray_fini(&cmd_buffer->vis_stream_bos);
   util_dynarray_fini(&cmd_buffer->vis_stream_cs_bos);

   vk_command_buffer_finish(&cmd_buffer->vk);
   vk_free2(&cmd_buffer->device->vk.alloc, &cmd_buffer->vk.pool->alloc,
            cmd_buffer);
}

static void
tu_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                    UNUSED VkCommandBufferResetFlags flags)
{
   struct tu_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct tu_cmd_buffer, vk);

   VkResult status_check_result = VK_SUCCESS;
   if (TU_DEBUG_START(CHECK_CMD_BUFFER_STATUS))
      status_check_result = tu_cmd_buffer_status_check_idle(cmd_buffer);

    vk_command_buffer_reset(&cmd_buffer->vk);

    if (TU_DEBUG_START(CHECK_CMD_BUFFER_STATUS) &&
        status_check_result != VK_SUCCESS) {
       cmd_buffer->vk.record_result = status_check_result;
    }

   tu_cs_reset(&cmd_buffer->cs);
   tu_cs_reset(&cmd_buffer->draw_cs);
   tu_cs_reset(&cmd_buffer->tile_store_cs);
   tu_cs_reset(&cmd_buffer->draw_epilogue_cs);
   tu_cs_reset(&cmd_buffer->sub_cs);
   tu_cs_reset(&cmd_buffer->pre_chain.draw_cs);
   tu_cs_reset(&cmd_buffer->pre_chain.draw_epilogue_cs);

   tu_autotune_free_results(cmd_buffer->device, &cmd_buffer->renderpass_autotune_results);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++) {
      memset(&cmd_buffer->descriptors[i].sets, 0, sizeof(cmd_buffer->descriptors[i].sets));
      if (cmd_buffer->descriptors[i].push_set.layout) {
         vk_descriptor_set_layout_unref(&cmd_buffer->device->vk,
                                        &cmd_buffer->descriptors[i].push_set.layout->vk);
      }
      vk_free(&cmd_buffer->device->vk.alloc, cmd_buffer->descriptors[i].push_set.mapped_ptr);
      memset(&cmd_buffer->descriptors[i].push_set, 0, sizeof(cmd_buffer->descriptors[i].push_set));
      cmd_buffer->descriptors[i].push_set.base.type = VK_OBJECT_TYPE_DESCRIPTOR_SET;
      cmd_buffer->descriptors[i].max_sets_bound = 0;
      cmd_buffer->descriptors[i].max_dynamic_offset_size = 0;
   }

   util_dynarray_foreach (&cmd_buffer->msrtss_color_temporaries,
                          struct tu_device_memory *, mem) {
      tu_destroy_memory(cmd_buffer->device, *mem);
   }
   util_dynarray_clear(&cmd_buffer->msrtss_color_temporaries);
   util_dynarray_foreach (&cmd_buffer->msrtss_depth_temporaries,
                          struct tu_device_memory *, mem) {
      tu_destroy_memory(cmd_buffer->device, *mem);
   }
   util_dynarray_clear(&cmd_buffer->msrtss_depth_temporaries);

   u_trace_fini(&cmd_buffer->trace);
   u_trace_init(&cmd_buffer->trace, &cmd_buffer->device->trace_context);
   u_trace_fini(&cmd_buffer->rp_trace);
   u_trace_init(&cmd_buffer->rp_trace, &cmd_buffer->device->trace_context);
   cmd_buffer->trace_renderpass_start =
      u_trace_begin_iterator(&cmd_buffer->rp_trace);

   cmd_buffer->state.max_vbs_bound = 0;

   cmd_buffer->vsc_initialized = false;
   cmd_buffer->prev_fsr_is_null = false;

   ralloc_free(cmd_buffer->patchpoints_ctx);
   ralloc_free(cmd_buffer->pre_chain.patchpoints_ctx);
   cmd_buffer->patchpoints_ctx = NULL;
   cmd_buffer->pre_chain.patchpoints_ctx = NULL;
   util_dynarray_clear(&cmd_buffer->fdm_bin_patchpoints);
   util_dynarray_clear(&cmd_buffer->pre_chain.fdm_bin_patchpoints);
   util_dynarray_clear(&cmd_buffer->vis_stream_patchpoints);
   util_dynarray_clear(&cmd_buffer->cb_control_points);

   util_dynarray_foreach (&cmd_buffer->vis_stream_bos, struct tu_bo *,
                          bo) {
      tu_bo_finish(cmd_buffer->device, *bo);
   }

   mtx_lock(&cmd_buffer->device->vis_stream_suballocator_mtx);
   util_dynarray_foreach (&cmd_buffer->vis_stream_cs_bos,
                          struct tu_vis_stream_patchpoint_cs,
                          bo) {
      tu_suballoc_bo_free(&cmd_buffer->device->vis_stream_suballocator,
                          &bo->cs_bo);
      tu_suballoc_bo_free(&cmd_buffer->device->vis_stream_suballocator,
                          &bo->fence_bo);
   }
   mtx_unlock(&cmd_buffer->device->vis_stream_suballocator_mtx);

   util_dynarray_clear(&cmd_buffer->vis_stream_bos);
   util_dynarray_clear(&cmd_buffer->vis_stream_cs_bos);
}

const struct vk_command_buffer_ops tu_cmd_buffer_ops = {
   .create = tu_create_cmd_buffer,
   .reset = tu_reset_cmd_buffer,
   .destroy = tu_cmd_buffer_destroy,
};

/* Initialize the cache, assuming all necessary flushes have happened but *not*
 * invalidations.
 */
static void
tu_cache_init(struct tu_cache_state *cache)
{
   cache->flush_bits = 0;
   cache->pending_flush_bits = TU_CMD_FLAG_ALL_INVALIDATE;
}

/* Unlike the public entrypoint, this doesn't handle cache tracking, and
 * tracking the CCU state. It's used for the driver to insert its own command
 * buffer in the middle of a submit.
 */
VkResult
tu_cmd_buffer_begin(struct tu_cmd_buffer *cmd_buffer,
                    const VkCommandBufferBeginInfo *pBeginInfo)
{
   vk_command_buffer_begin(&cmd_buffer->vk, pBeginInfo);

   memset(&cmd_buffer->state, 0, sizeof(cmd_buffer->state));
   vk_dynamic_graphics_state_init(&cmd_buffer->vk.dynamic_graphics_state);
   cmd_buffer->vk.dynamic_graphics_state.vi = &cmd_buffer->state.vi;
   cmd_buffer->vk.dynamic_graphics_state.ms.sample_locations = &cmd_buffer->state.sl;
   cmd_buffer->state.index_size = 0xff; /* dirty restart index */
   cmd_buffer->state.gmem_layout = TU_GMEM_LAYOUT_COUNT; /* dirty value */

   tu_cache_init(&cmd_buffer->state.cache);
   tu_cache_init(&cmd_buffer->state.renderpass_cache);
   cmd_buffer->usage_flags = pBeginInfo->flags;

   tu_cs_begin(&cmd_buffer->cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      if (u_trace_enabled(&cmd_buffer->device->trace_context)) {
         trace_start_cmd_buffer(&cmd_buffer->trace, &cmd_buffer->cs,
                                cmd_buffer, tu_env_debug_as_string(),
                                ir3_shader_debug_as_string());
      }
   }

   tu_cmd_buffer_status_gpu_write(cmd_buffer, TU_CMD_BUFFER_STATUS_ACTIVE);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                      const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result = tu_cmd_buffer_begin(cmd_buffer, pBeginInfo);
   if (result != VK_SUCCESS)
      return result;

   /* setup initial configuration into command buffer */
   if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      switch (cmd_buffer->queue_family_index) {
      case TU_QUEUE_GENERAL:
         TU_CALLX(cmd_buffer->device, tu6_init_hw)(cmd_buffer, &cmd_buffer->cs);
         break;
      default:
         break;
      }
   } else if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
      const bool pass_continue =
         pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

      if (u_trace_enabled(&cmd_buffer->device->trace_context)) {
         trace_start_secondary_cmd_buffer(
            pass_continue ? &cmd_buffer->rp_trace : &cmd_buffer->trace,
            pass_continue ? &cmd_buffer->draw_cs : &cmd_buffer->cs,
            cmd_buffer);
      }

      assert(pBeginInfo->pInheritanceInfo);

      cmd_buffer->inherited_pipeline_statistics =
         pBeginInfo->pInheritanceInfo->pipelineStatistics;

      cmd_buffer->state.occlusion_query_may_be_running =
         pBeginInfo->pInheritanceInfo->occlusionQueryEnable;

      vk_foreach_struct_const(ext, pBeginInfo->pInheritanceInfo) {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_CONDITIONAL_RENDERING_INFO_EXT: {
            const VkCommandBufferInheritanceConditionalRenderingInfoEXT *cond_rend =
               (VkCommandBufferInheritanceConditionalRenderingInfoEXT *) ext;
            cmd_buffer->state.predication_active = cond_rend->conditionalRenderingEnable;
            break;
         }
         default:
            break;
         }
      }

      if (pass_continue) {
         const VkCommandBufferInheritanceRenderingInfo *rendering_info =
            vk_find_struct_const(pBeginInfo->pInheritanceInfo->pNext,
                                 COMMAND_BUFFER_INHERITANCE_RENDERING_INFO);

         if (TU_DEBUG(DYNAMIC)) {
            rendering_info =
               vk_get_command_buffer_inheritance_rendering_info(cmd_buffer->vk.level,
                                                                pBeginInfo);
         }

         if (rendering_info) {
            tu_setup_dynamic_inheritance(cmd_buffer, rendering_info);
            cmd_buffer->state.pass = &cmd_buffer->dynamic_pass;
            cmd_buffer->state.subpass = &cmd_buffer->dynamic_subpasses[0];

            const VkRenderingAttachmentLocationInfoKHR *location_info =
               vk_find_struct_const(pBeginInfo->pInheritanceInfo->pNext,
                                    RENDERING_ATTACHMENT_LOCATION_INFO_KHR);
            if (location_info) {
               vk_common_CmdSetRenderingAttachmentLocationsKHR(commandBuffer,
                                                               location_info);
            }
            /* Unfortunately with dynamic renderpasses we get no indication
             * whether FDM is used in secondaries, so we have to assume it
             * always might be enabled.
             */
            cmd_buffer->state.fdm_enabled = 
               cmd_buffer->device->vk.enabled_features.fragmentDensityMap ||
               TU_DEBUG(FDM);
         } else {
            cmd_buffer->state.pass = tu_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);
            cmd_buffer->state.subpass =
               &cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];
            cmd_buffer->state.fdm_enabled = cmd_buffer->state.pass->has_fdm;
         }
         tu_fill_render_pass_state(&cmd_buffer->state.vk_rp,
                                   cmd_buffer->state.pass,
                                   cmd_buffer->state.subpass);
         vk_cmd_set_cb_attachment_count(&cmd_buffer->vk,
                                        cmd_buffer->state.subpass->color_count);
         cmd_buffer->state.dirty |= TU_CMD_DIRTY_SUBPASS;

         cmd_buffer->patchpoints_ctx = ralloc_context(NULL);

         /* We can't set the gmem layout here, because the state.pass only has
          * to be compatible (same formats/sample counts) with the primary's
          * renderpass, rather than exactly equal.
          */

         tu_lrz_begin_secondary_cmdbuf(cmd_buffer);
      } else {
         /* When executing in the middle of another command buffer, the CCU
          * state is unknown.
          */
         cmd_buffer->state.ccu_state = TU_CMD_CCU_UNKNOWN;
      }
   }

   return VK_SUCCESS;
}

static struct tu_cs
tu_cmd_dynamic_state(struct tu_cmd_buffer *cmd, uint32_t id, uint32_t size)
{
   struct tu_cs cs;

   assert(id < ARRAY_SIZE(cmd->state.dynamic_state));
   cmd->state.dynamic_state[id] = tu_cs_draw_state(&cmd->sub_cs, &cs, size);

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)
      return cs;

   tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit_draw_state(&cmd->draw_cs, TU_DRAW_STATE_DYNAMIC + id, cmd->state.dynamic_state[id]);

   return cs;
}

static void
tu_cmd_end_dynamic_state(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                         uint32_t id)
{
   assert(id < ARRAY_SIZE(cmd->state.dynamic_state));
   cmd->state.dynamic_state[id] = tu_cs_end_draw_state(&cmd->sub_cs, cs);

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)
      return;

   tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit_draw_state(&cmd->draw_cs, TU_DRAW_STATE_DYNAMIC + id, cmd->state.dynamic_state[id]);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer,
                         uint32_t firstBinding,
                         uint32_t bindingCount,
                         const VkBuffer *pBuffers,
                         const VkDeviceSize *pOffsets,
                         const VkDeviceSize *pSizes,
                         const VkDeviceSize *pStrides)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs;

   cmd->state.max_vbs_bound = MAX2(
      cmd->state.max_vbs_bound, firstBinding + bindingCount);

   if (pStrides) {
      vk_cmd_set_vertex_binding_strides(&cmd->vk, firstBinding, bindingCount,
                                        pStrides);
   }

   cmd->state.vertex_buffers.iova =
      tu_cs_draw_state(&cmd->sub_cs, &cs, 4 * cmd->state.max_vbs_bound).iova;

   for (uint32_t i = 0; i < bindingCount; i++) {
      if (pBuffers[i] == VK_NULL_HANDLE) {
         cmd->state.vb[firstBinding + i].base = 0;
         cmd->state.vb[firstBinding + i].size = 0;
      } else {
         struct tu_buffer *buf = tu_buffer_from_handle(pBuffers[i]);
         cmd->state.vb[firstBinding + i].base = vk_buffer_address(&buf->vk, pOffsets[i]);
         cmd->state.vb[firstBinding + i].size =
            vk_buffer_range(&buf->vk, pOffsets[i], pSizes ? pSizes[i] : VK_WHOLE_SIZE);
      }
   }

   for (uint32_t i = 0; i < cmd->state.max_vbs_bound; i++) {
      tu_cs_emit_regs(&cs,
                      A6XX_VFD_VERTEX_BUFFER_BASE(i, .qword = cmd->state.vb[i].base),
                      A6XX_VFD_VERTEX_BUFFER_SIZE(i, cmd->state.vb[i].size));
   }

   cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer,
                          VkBuffer buffer,
                          VkDeviceSize offset,
                          VkDeviceSize size,
                          VkIndexType indexType)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buf, buffer);

   size = buf ? vk_buffer_range(&buf->vk, offset, size) : 0;

   uint32_t index_size, index_shift;
   uint32_t restart_index = vk_index_to_restart(indexType);

   switch (indexType) {
   case VK_INDEX_TYPE_UINT16:
      index_size = INDEX4_SIZE_16_BIT;
      index_shift = 1;
      break;
   case VK_INDEX_TYPE_UINT32:
      index_size = INDEX4_SIZE_32_BIT;
      index_shift = 2;
      break;
   case VK_INDEX_TYPE_UINT8_KHR:
      index_size = INDEX4_SIZE_8_BIT;
      index_shift = 0;
      break;
   default:
      UNREACHABLE("invalid VkIndexType");
   }

   if (buf) {
      /* initialize/update the restart index */
      if (cmd->state.index_size != index_size)
         tu_cs_emit_regs(&cmd->draw_cs, PC_RESTART_INDEX(CHIP, restart_index));

      cmd->state.index_va = vk_buffer_address(&buf->vk, offset);
      cmd->state.max_index_count = size >> index_shift;
      cmd->state.index_size = index_size;
   } else {
      cmd->state.index_va = 0;
      cmd->state.max_index_count = 0;
      cmd->state.index_size = 0;
   }
}
TU_GENX(tu_CmdBindIndexBuffer2KHR);

template <chip CHIP>
static void
tu6_emit_descriptor_sets(struct tu_cmd_buffer *cmd,
                         VkPipelineBindPoint bind_point)
{
   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd, bind_point);
   uint32_t sp_bindless_base_reg, hlsq_bindless_base_reg;
   struct tu_cs *cs, state_cs;

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      sp_bindless_base_reg = __SP_GFX_BINDLESS_BASE_DESCRIPTOR<CHIP>(0, {}).reg;
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_BINDLESS_BASE(0);

      if (CHIP == A6XX) {
         cmd->state.desc_sets =
            tu_cs_draw_state(&cmd->sub_cs, &state_cs,
                             4 + 4 * descriptors_state->max_sets_bound +
                             (descriptors_state->max_dynamic_offset_size ? 6 : 0));
      } else {
         cmd->state.desc_sets =
            tu_cs_draw_state(&cmd->sub_cs, &state_cs,
                             3 + 2 * descriptors_state->max_sets_bound +
                             (descriptors_state->max_dynamic_offset_size ? 3 : 0));
      }
      cs = &state_cs;
   } else {
      assert(bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);

      sp_bindless_base_reg = __SP_CS_BINDLESS_BASE_DESCRIPTOR<CHIP>(0, {}).reg;
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_CS_BINDLESS_BASE(0);

      cs = &cmd->cs;
   }

   tu_cs_emit_pkt4(cs, sp_bindless_base_reg, 2 * descriptors_state->max_sets_bound);
   tu_cs_emit_array(cs, (const uint32_t*)descriptors_state->set_iova, 2 * descriptors_state->max_sets_bound);
   if (CHIP == A6XX) {
      tu_cs_emit_pkt4(cs, hlsq_bindless_base_reg, 2 * descriptors_state->max_sets_bound);
      tu_cs_emit_array(cs, (const uint32_t*)descriptors_state->set_iova, 2 * descriptors_state->max_sets_bound);
   }

   /* Dynamic descriptors get the reserved descriptor set. */
   if (descriptors_state->max_dynamic_offset_size) {
      int reserved_set_idx = cmd->device->physical_device->reserved_set_idx;
      assert(reserved_set_idx >= 0); /* reserved set must be bound */

      tu_cs_emit_pkt4(cs, sp_bindless_base_reg + reserved_set_idx * 2, 2);
      tu_cs_emit_qw(cs, descriptors_state->set_iova[reserved_set_idx]);
      if (CHIP == A6XX) {
         tu_cs_emit_pkt4(cs, hlsq_bindless_base_reg + reserved_set_idx * 2, 2);
         tu_cs_emit_qw(cs, descriptors_state->set_iova[reserved_set_idx]);
      }
   }

   tu_cs_emit_regs(cs, SP_UPDATE_CNTL(CHIP,
      .cs_bindless = bind_point == VK_PIPELINE_BIND_POINT_COMPUTE ? CHIP == A6XX ? 0x1f : 0xff : 0,
      .gfx_bindless = bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS ? CHIP == A6XX ? 0x1f : 0xff : 0,
   ));

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      assert(cs->cur == cs->end); /* validate draw state size */
      /* note: this also avoids emitting draw states before renderpass clears,
       * which may use the 3D clear path (for MSAA cases)
       */
      if (!(cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
         tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
         tu_cs_emit_draw_state(&cmd->draw_cs, TU_DRAW_STATE_DESC_SETS, cmd->state.desc_sets);
      }
   }
}

/* We lazily emit the draw state for desciptor sets at draw time, so that we can
 * batch together multiple tu_CmdBindDescriptorSets() calls.  ANGLE and zink
 * will often emit multiple bind calls in a draw.
 */
static void
tu_dirty_desc_sets(struct tu_cmd_buffer *cmd,
                   VkPipelineBindPoint pipelineBindPoint)
{
   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
      cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_DESC_SETS;
   } else {
      assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);
      cmd->state.dirty |= TU_CMD_DIRTY_DESC_SETS;
   }
}

static void
tu_bind_descriptor_sets(struct tu_cmd_buffer *cmd,
                        const VkBindDescriptorSetsInfoKHR *info,
                        VkPipelineBindPoint bind_point)
{
   VK_FROM_HANDLE(tu_pipeline_layout, layout, info->layout);
   unsigned dyn_idx = 0;

   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd, bind_point);

   descriptors_state->max_sets_bound =
      MAX2(descriptors_state->max_sets_bound,
           info->firstSet + info->descriptorSetCount);

   unsigned dynamic_offset_offset = 0;
   for (unsigned i = 0; i < info->firstSet; i++) {
      if (layout->set[i].layout)
         dynamic_offset_offset += layout->set[i].layout->dynamic_offset_size;
   }

   for (unsigned i = 0; i < info->descriptorSetCount; ++i) {
      unsigned idx = i + info->firstSet;
      VK_FROM_HANDLE(tu_descriptor_set, set, info->pDescriptorSets[i]);

      descriptors_state->sets[idx] = set;
      descriptors_state->set_iova[idx] = set ?
         (set->va | BINDLESS_DESCRIPTOR_64B) : 0;

      if (!set)
         continue;

      if (set->layout->has_inline_uniforms)
         cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS;

      if (!set->layout->dynamic_offset_size)
         continue;

      uint32_t *src = set->dynamic_descriptors;
      uint32_t *dst = descriptors_state->dynamic_descriptors +
         dynamic_offset_offset / 4;
      for (unsigned j = 0; j < set->layout->binding_count; j++) {
         struct tu_descriptor_set_binding_layout *binding =
            &set->layout->binding[j];
         if (vk_descriptor_type_is_dynamic(binding->type)) {
            for (unsigned k = 0; k < binding->array_size; k++, dyn_idx++) {
               assert(dyn_idx < info->dynamicOffsetCount);
               uint32_t offset = info->pDynamicOffsets[dyn_idx];
               memcpy(dst, src, binding->size);

               if (binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                  /* Note: we can assume here that the addition won't roll
                   * over and change the SIZE field.
                   */
                  uint64_t va = src[0] | ((uint64_t)src[1] << 32);
                  va += offset;
                  dst[0] = va;
                  dst[1] = va >> 32;
               } else {
                  uint32_t *dst_desc = dst;
                  for (unsigned i = 0;
                       i < binding->size / (4 * A6XX_TEX_CONST_DWORDS);
                       i++, dst_desc += A6XX_TEX_CONST_DWORDS) {
                     /* Note: A6XX_TEX_CONST_5_DEPTH is always 0 */
                     uint64_t va = dst_desc[4] | ((uint64_t)dst_desc[5] << 32);
                     uint32_t desc_offset = pkt_field_get(
                        A6XX_TEX_CONST_2_STARTOFFSETTEXELS, dst_desc[2]);

                     /* Use descriptor's format to determine the shift amount
                      * that's to be used on the offset value.
                      */
                     uint32_t format =
                        pkt_field_get(A6XX_TEX_CONST_0_FMT, dst_desc[0]);
                     unsigned offset_shift;
                     switch (format) {
                     case FMT6_16_UINT:
                        offset_shift = 1;
                        break;
                     case FMT6_32_UINT:
                        offset_shift = 2;
                        break;
                     case FMT6_8_UINT:
                     default:
                        offset_shift = 0;
                        break;
                     }

                     va += desc_offset << offset_shift;
                     va += offset;
                     unsigned new_offset = (va & 0x3f) >> offset_shift;
                     va &= ~0x3full;
                     dst_desc[4] = va;
                     dst_desc[5] = va >> 32;
                     dst_desc[2] =
                        pkt_field_set(A6XX_TEX_CONST_2_STARTOFFSETTEXELS,
                                      dst_desc[2], new_offset);
                  }
               }

               dst += binding->size / 4;
               src += binding->size / 4;
            }
         }
      }

      if (layout->set[idx].layout)
         dynamic_offset_offset += layout->set[idx].layout->dynamic_offset_size;
   }
   assert(dyn_idx == info->dynamicOffsetCount);

   if (dynamic_offset_offset) {
      descriptors_state->max_dynamic_offset_size =
         MAX2(descriptors_state->max_dynamic_offset_size, dynamic_offset_offset);

      /* allocate and fill out dynamic descriptor set */
      struct tu_cs_memory dynamic_desc_set;
      int reserved_set_idx = cmd->device->physical_device->reserved_set_idx;
      VkResult result =
         tu_cs_alloc(&cmd->sub_cs,
                     descriptors_state->max_dynamic_offset_size /
                     (4 * A6XX_TEX_CONST_DWORDS),
                     A6XX_TEX_CONST_DWORDS, &dynamic_desc_set);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }

      memcpy(dynamic_desc_set.map, descriptors_state->dynamic_descriptors,
             descriptors_state->max_dynamic_offset_size);
      assert(reserved_set_idx >= 0); /* reserved set must be bound */
      descriptors_state->set_iova[reserved_set_idx] = dynamic_desc_set.iova | BINDLESS_DESCRIPTOR_64B;
   }

   tu_dirty_desc_sets(cmd, bind_point);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindDescriptorSets2KHR(
   VkCommandBuffer commandBuffer,
   const VkBindDescriptorSetsInfoKHR *pBindDescriptorSetsInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      tu_bind_descriptor_sets(cmd, pBindDescriptorSetsInfo,
                              VK_PIPELINE_BIND_POINT_COMPUTE);
   }

   if (pBindDescriptorSetsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      tu_bind_descriptor_sets(cmd, pBindDescriptorSetsInfo,
                              VK_PIPELINE_BIND_POINT_GRAPHICS);
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindDescriptorBuffersEXT(
   VkCommandBuffer commandBuffer,
   uint32_t bufferCount,
   const VkDescriptorBufferBindingInfoEXT *pBindingInfos)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   for (unsigned i = 0; i < bufferCount; i++)
      cmd->state.descriptor_buffer_iova[i] = pBindingInfos[i].address;
}

static void
tu_set_descriptor_buffer_offsets(
   struct tu_cmd_buffer *cmd,
   const VkSetDescriptorBufferOffsetsInfoEXT *info,
   VkPipelineBindPoint bind_point)
{
   VK_FROM_HANDLE(tu_pipeline_layout, layout, info->layout);

   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd, bind_point);

   descriptors_state->max_sets_bound = MAX2(descriptors_state->max_sets_bound,
                                            info->firstSet + info->setCount);

   for (unsigned i = 0; i < info->setCount; ++i) {
      unsigned idx = i + info->firstSet;
      struct tu_descriptor_set_layout *set_layout = layout->set[idx].layout;

      descriptors_state->set_iova[idx] =
         (cmd->state.descriptor_buffer_iova[info->pBufferIndices[i]] +
          info->pOffsets[i]) |
         BINDLESS_DESCRIPTOR_64B;

      if (set_layout->has_inline_uniforms)
         cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS;
   }

   tu_dirty_desc_sets(cmd, bind_point);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDescriptorBufferOffsets2EXT(
   VkCommandBuffer commandBuffer,
   const VkSetDescriptorBufferOffsetsInfoEXT *pSetDescriptorBufferOffsetsInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (pSetDescriptorBufferOffsetsInfo->stageFlags &
       VK_SHADER_STAGE_COMPUTE_BIT) {
      tu_set_descriptor_buffer_offsets(cmd, pSetDescriptorBufferOffsetsInfo,
                                       VK_PIPELINE_BIND_POINT_COMPUTE);
   }

   if (pSetDescriptorBufferOffsetsInfo->stageFlags &
       VK_SHADER_STAGE_ALL_GRAPHICS) {
      tu_set_descriptor_buffer_offsets(cmd, pSetDescriptorBufferOffsetsInfo,
                                       VK_PIPELINE_BIND_POINT_GRAPHICS);
   }
}

static void
tu_bind_descriptor_buffer_embedded_samplers(
   struct tu_cmd_buffer *cmd,
   const VkBindDescriptorBufferEmbeddedSamplersInfoEXT *info,
   VkPipelineBindPoint bind_point)
{
   VK_FROM_HANDLE(tu_pipeline_layout, layout, info->layout);

   struct tu_descriptor_set_layout *set_layout =
      layout->set[info->set].layout;

   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd, bind_point);

   descriptors_state->max_sets_bound =
      MAX2(descriptors_state->max_sets_bound, info->set + 1);

   descriptors_state->set_iova[info->set] =
      set_layout->embedded_samplers->iova | BINDLESS_DESCRIPTOR_64B;

   tu_dirty_desc_sets(cmd, bind_point);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindDescriptorBufferEmbeddedSamplers2EXT(
   VkCommandBuffer commandBuffer,
   const VkBindDescriptorBufferEmbeddedSamplersInfoEXT
      *pBindDescriptorBufferEmbeddedSamplersInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (pBindDescriptorBufferEmbeddedSamplersInfo->stageFlags &
       VK_SHADER_STAGE_COMPUTE_BIT) {
      tu_bind_descriptor_buffer_embedded_samplers(
         cmd, pBindDescriptorBufferEmbeddedSamplersInfo,
         VK_PIPELINE_BIND_POINT_COMPUTE);
   }

   if (pBindDescriptorBufferEmbeddedSamplersInfo->stageFlags &
       VK_SHADER_STAGE_ALL_GRAPHICS) {
      tu_bind_descriptor_buffer_embedded_samplers(
         cmd, pBindDescriptorBufferEmbeddedSamplersInfo,
         VK_PIPELINE_BIND_POINT_GRAPHICS);
   }
}

static VkResult
tu_push_descriptor_set_update_layout(struct tu_device *device,
                                     struct tu_descriptor_set *set,
                                     struct tu_descriptor_set_layout *layout)
{
   if (set->layout == layout)
      return VK_SUCCESS;

   if (set->layout)
      vk_descriptor_set_layout_unref(&device->vk, &set->layout->vk);
   vk_descriptor_set_layout_ref(&layout->vk);
   set->layout = layout;

   if (set->host_size < layout->size) {
      void *new_buf =
         vk_realloc(&device->vk.alloc, set->mapped_ptr, layout->size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!new_buf)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      set->mapped_ptr = (uint32_t *) new_buf;
      set->host_size = layout->size;
   }
   return VK_SUCCESS;
}

static void
tu_push_descriptor_set(struct tu_cmd_buffer *cmd,
                       const VkPushDescriptorSetInfoKHR *info,
                       VkPipelineBindPoint bind_point)
{
   VK_FROM_HANDLE(tu_pipeline_layout, pipe_layout, info->layout);
   struct tu_descriptor_set_layout *layout =
      pipe_layout->set[info->set].layout;
   struct tu_descriptor_set *set =
      &tu_get_descriptors_state(cmd, bind_point)->push_set;

   struct tu_cs_memory set_mem;
   VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                 DIV_ROUND_UP(layout->size, A6XX_TEX_CONST_DWORDS * 4),
                                 A6XX_TEX_CONST_DWORDS, &set_mem);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   result = tu_push_descriptor_set_update_layout(cmd->device, set, layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_update_descriptor_sets(cmd->device, tu_descriptor_set_to_handle(set),
                             info->descriptorWriteCount,
                             info->pDescriptorWrites, 0, NULL);

   memcpy(set_mem.map, set->mapped_ptr, layout->size);
   set->va = set_mem.iova;

   const VkDescriptorSet desc_set[] = { tu_descriptor_set_to_handle(set) };
   vk_common_CmdBindDescriptorSets(tu_cmd_buffer_to_handle(cmd), bind_point,
                                   info->layout, info->set, 1, desc_set, 0,
                                   NULL);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPushDescriptorSet2KHR(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetInfoKHR *pPushDescriptorSetInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT) {
      tu_push_descriptor_set(cmd, pPushDescriptorSetInfo,
                             VK_PIPELINE_BIND_POINT_COMPUTE);
   }

   if (pPushDescriptorSetInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS) {
      tu_push_descriptor_set(cmd, pPushDescriptorSetInfo,
                             VK_PIPELINE_BIND_POINT_GRAPHICS);
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPushDescriptorSetWithTemplate2KHR(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetWithTemplateInfoKHR
      *pPushDescriptorSetWithTemplateInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_pipeline_layout, pipe_layout,
                  pPushDescriptorSetWithTemplateInfo->layout);
   VK_FROM_HANDLE(
      tu_descriptor_update_template, templ,
      pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate);
   struct tu_descriptor_set_layout *layout =
      pipe_layout->set[pPushDescriptorSetWithTemplateInfo->set].layout;
   struct tu_descriptor_set *set =
      &tu_get_descriptors_state(cmd, templ->bind_point)->push_set;

   struct tu_cs_memory set_mem;
   VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                 DIV_ROUND_UP(layout->size, A6XX_TEX_CONST_DWORDS * 4),
                                 A6XX_TEX_CONST_DWORDS, &set_mem);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   result = tu_push_descriptor_set_update_layout(cmd->device, set, layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_update_descriptor_set_with_template(
      cmd->device, set,
      pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate,
      pPushDescriptorSetWithTemplateInfo->pData);

   memcpy(set_mem.map, set->mapped_ptr, layout->size);
   set->va = set_mem.iova;

   const VkDescriptorSet desc_set[] = { tu_descriptor_set_to_handle(set) };
   vk_common_CmdBindDescriptorSets(
      tu_cmd_buffer_to_handle(cmd), templ->bind_point,
      pPushDescriptorSetWithTemplateInfo->layout,
      pPushDescriptorSetWithTemplateInfo->set, 1, desc_set, 0, NULL);
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
                                      uint32_t firstBinding,
                                      uint32_t bindingCount,
                                      const VkBuffer *pBuffers,
                                      const VkDeviceSize *pOffsets,
                                      const VkDeviceSize *pSizes)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   /* using COND_REG_EXEC for xfb commands matches the blob behavior
    * presumably there isn't any benefit using a draw state when the
    * condition is (SYSMEM | BINNING)
    */
   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_SYSMEM |
                          CP_COND_REG_EXEC_0_BINNING);

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(tu_buffer, buf, pBuffers[i]);
      uint64_t iova = vk_buffer_address(&buf->vk, pOffsets[i]);
      uint32_t size = vk_buffer_range(&buf->vk, pOffsets[i],
                                      pSizes ? pSizes[i] : VK_WHOLE_SIZE);
      uint32_t idx = i + firstBinding;

      /* BUFFER_BASE is 32-byte aligned, add remaining offset to BUFFER_OFFSET */
      uint32_t offset = iova & 0x1f;
      iova &= ~(uint64_t) 0x1f;

      tu_cs_emit_regs(cs, VPC_SO_BUFFER_BASE(CHIP, idx, .qword = iova),
                      VPC_SO_BUFFER_SIZE(CHIP, idx, size + offset));

      cmd->state.streamout_offset[idx] = offset;
   }

   tu_cond_exec_end(cs);
}
TU_GENX(tu_CmdBindTransformFeedbackBuffersEXT);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                uint32_t firstCounterBuffer,
                                uint32_t counterBufferCount,
                                const VkBuffer *pCounterBuffers,
                                const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_SYSMEM |
                          CP_COND_REG_EXEC_0_BINNING);

   tu_cs_emit_regs(cs, VPC_SO_OVERRIDE(CHIP, false));

   /* TODO: only update offset for active buffers */
   for (uint32_t i = 0; i < IR3_MAX_SO_BUFFERS; i++)
      tu_cs_emit_regs(cs, VPC_SO_BUFFER_OFFSET(CHIP, i, cmd->state.streamout_offset[i]));

   for (uint32_t i = 0; i < (pCounterBuffers ? counterBufferCount : 0); i++) {
      uint32_t idx = firstCounterBuffer + i;
      uint32_t offset = cmd->state.streamout_offset[idx];
      uint64_t counter_buffer_offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0u;

      if (!pCounterBuffers[i])
         continue;

      VK_FROM_HANDLE(tu_buffer, buf, pCounterBuffers[i]);

      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(VPC_SO_BUFFER_OFFSET(CHIP, idx).reg) |
                     CP_MEM_TO_REG_0_UNK31 |
                     CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, counter_buffer_offset));

      if (offset) {
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs, CP_REG_RMW_0_DST_REG(VPC_SO_BUFFER_OFFSET(CHIP, idx).reg) |
                        CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit(cs, 0xffffffff);
         tu_cs_emit(cs, offset);
      }
   }

   tu_cond_exec_end(cs);
}
TU_GENX(tu_CmdBeginTransformFeedbackEXT);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                              uint32_t firstCounterBuffer,
                              uint32_t counterBufferCount,
                              const VkBuffer *pCounterBuffers,
                              const VkDeviceSize *pCounterBufferOffsets)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_SYSMEM |
                          CP_COND_REG_EXEC_0_BINNING);

   tu_cs_emit_regs(cs, VPC_SO_OVERRIDE(CHIP, true));

   /* TODO: only flush buffers that need to be flushed */
   for (uint32_t i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
      /* note: FLUSH_BASE is always the same, so it could go in init_hw()? */
      tu_cs_emit_regs(cs, VPC_SO_FLUSH_BASE(CHIP, i, .qword = global_iova_arr(cmd, flush_base, i)));
      tu_emit_event_write<CHIP>(cmd, cs, (enum fd_gpu_event) (FD_FLUSH_SO_0 + i));
   }

   for (uint32_t i = 0; i < (pCounterBuffers ? counterBufferCount : 0); i++) {
      uint32_t idx = firstCounterBuffer + i;
      uint32_t offset = cmd->state.streamout_offset[idx];
      uint64_t counter_buffer_offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0u;

      if (!pCounterBuffers[i])
         continue;

      VK_FROM_HANDLE(tu_buffer, buf, pCounterBuffers[i]);

      /* VPC_SO_FLUSH_BASE has dwords counter, but counter should be in bytes */
      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_CP_SCRATCH(0)) |
                     COND(CHIP == A6XX, CP_MEM_TO_REG_0_SHIFT_BY_2) |
                     0x40000 | /* ??? */
                     CP_MEM_TO_REG_0_UNK31 |
                     CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, global_iova_arr(cmd, flush_base, idx));

      if (offset) {
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs, CP_REG_RMW_0_DST_REG(REG_A6XX_CP_SCRATCH(0)) |
                        CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit(cs, 0xffffffff);
         tu_cs_emit(cs, -offset);
      }

      tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
      tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_CP_SCRATCH(0)) |
                     CP_REG_TO_MEM_0_CNT(1));
      tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, counter_buffer_offset));
   }

   tu_cond_exec_end(cs);

   cmd->state.rp.xfb_used = true;
}
TU_GENX(tu_CmdEndTransformFeedbackEXT);

VKAPI_ATTR void VKAPI_CALL
tu_CmdPushConstants2KHR(VkCommandBuffer commandBuffer,
                        const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   memcpy((char *) cmd->push_constants + pPushConstantsInfo->offset,
          pPushConstantsInfo->pValues, pPushConstantsInfo->size);
   cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS;
}

/* Clean everything which has been made available but we haven't actually
 * cleaned yet.
 */
static void
tu_clean_all_pending(struct tu_cache_state *cache)
{
   cache->flush_bits |= cache->pending_flush_bits & TU_CMD_FLAG_ALL_CLEAN;
   cache->pending_flush_bits &= ~TU_CMD_FLAG_ALL_CLEAN;
}

template <chip CHIP>
VKAPI_ATTR VkResult VKAPI_CALL
tu_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   /* We currently flush CCU at the end of the command buffer, like
    * what the blob does. There's implicit synchronization around every
    * vkQueueSubmit, but the kernel only flushes the UCHE, and we don't
    * know yet if this command buffer will be the last in the submit so we
    * have to defensively flush everything else.
    *
    * TODO: We could definitely do better than this, since these flushes
    * aren't required by Vulkan, but we'd need kernel support to do that.
    * Ideally, we'd like the kernel to flush everything afterwards, so that we
    * wouldn't have to do any flushes here, and when submitting multiple
    * command buffers there wouldn't be any unnecessary flushes in between.
    */
   if (cmd_buffer->state.pass) {
      tu_clean_all_pending(&cmd_buffer->state.renderpass_cache);
      tu_emit_cache_flush_renderpass<CHIP>(cmd_buffer);
   } else {
      tu_clean_all_pending(&cmd_buffer->state.cache);
      cmd_buffer->state.cache.flush_bits |=
         TU_CMD_FLAG_CCU_CLEAN_COLOR |
         TU_CMD_FLAG_CCU_CLEAN_DEPTH;
      tu_emit_cache_flush<CHIP>(cmd_buffer);
   }

   if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      trace_end_cmd_buffer(&cmd_buffer->trace, &cmd_buffer->cs, cmd_buffer);
   } else {
      trace_end_secondary_cmd_buffer(
         cmd_buffer->state.pass ? &cmd_buffer->rp_trace : &cmd_buffer->trace,
         cmd_buffer->state.pass ? &cmd_buffer->draw_cs : &cmd_buffer->cs);
   }

   if (TU_DEBUG_START(CHECK_CMD_BUFFER_STATUS))
      tu_cmd_buffer_status_gpu_write(cmd_buffer, TU_CMD_BUFFER_STATUS_IDLE);

   tu_cs_end(&cmd_buffer->cs);
   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);

   return vk_command_buffer_end(&cmd_buffer->vk);
}
TU_GENX(tu_EndCommandBuffer);

static void
tu_bind_vs(struct tu_cmd_buffer *cmd, struct tu_shader *vs)
{
   cmd->state.shaders[MESA_SHADER_VERTEX] = vs;
}

static void
tu_bind_tcs(struct tu_cmd_buffer *cmd, struct tu_shader *tcs)
{
   cmd->state.shaders[MESA_SHADER_TESS_CTRL] = tcs;
}

static void
tu_bind_tes(struct tu_cmd_buffer *cmd, struct tu_shader *tes)
{
   if (cmd->state.shaders[MESA_SHADER_TESS_EVAL] != tes) {
      cmd->state.shaders[MESA_SHADER_TESS_EVAL] = tes;
      cmd->state.dirty |= TU_CMD_DIRTY_TES;

      if (!cmd->state.tess_params.valid ||
          cmd->state.tess_params.output_upper_left !=
          tes->tes.tess_output_upper_left ||
          cmd->state.tess_params.output_lower_left !=
          tes->tes.tess_output_lower_left ||
          cmd->state.tess_params.spacing != tes->tes.tess_spacing) {
         cmd->state.tess_params.output_upper_left =
            tes->tes.tess_output_upper_left;
         cmd->state.tess_params.output_lower_left =
            tes->tes.tess_output_lower_left;
         cmd->state.tess_params.spacing = tes->tes.tess_spacing;
         cmd->state.tess_params.valid = true;
         cmd->state.dirty |= TU_CMD_DIRTY_TESS_PARAMS;
      }
   }
}

static void
tu_bind_gs(struct tu_cmd_buffer *cmd, struct tu_shader *gs)
{
   cmd->state.shaders[MESA_SHADER_GEOMETRY] = gs;
}

static void
tu_bind_fs(struct tu_cmd_buffer *cmd, struct tu_shader *fs)
{
   if (cmd->state.shaders[MESA_SHADER_FRAGMENT] != fs) {
      cmd->state.shaders[MESA_SHADER_FRAGMENT] = fs;
      cmd->state.dirty |= TU_CMD_DIRTY_LRZ | TU_CMD_DIRTY_FS;
   }
}

/* We cannot do this only at pipeline bind time since pipeline
 * could have been bound at any time before current renderpass,
 * e.g. in the previous renderpass.
 */
static void
tu_pipeline_update_rp_state(struct tu_cmd_state *cmd_state)
{
   if (cmd_state->pipeline_disable_gmem &&
       !cmd_state->rp.disable_gmem) {
      /* VK_EXT_attachment_feedback_loop_layout allows feedback loop to involve
       * not only input attachments but also sampled images or image resources.
       * But we cannot just patch gmem for image in the descriptors.
       *
       * At the moment, in context of DXVK, it is expected that only a few
       * drawcalls in a frame would use feedback loop and they would be wrapped
       * in their own renderpasses, so it should be ok to force sysmem.
       *
       * However, there are two further possible optimizations if need would
       * arise for other translation layer:
       * - Tiling could be enabled if we ensure that there is no barrier in
       *   the renderpass;
       * - Check that both pipeline and attachments agree that feedback loop
       *   is needed.
       */
      perf_debug(
         cmd->device,
         "Disabling gmem due to VK_EXT_attachment_feedback_loop_layout");
      cmd_state->rp.disable_gmem = true;
      cmd_state->rp.gmem_disable_reason =
         "VK_EXT_attachment_feedback_loop_layout may involve textures";
   }

   if (cmd_state->pipeline_sysmem_single_prim_mode &&
       !cmd_state->rp.sysmem_single_prim_mode) {
      perf_debug(cmd->device, "single_prim_mode due to pipeline settings");
      cmd_state->rp.sysmem_single_prim_mode = true;
   }

   if (cmd_state->pipeline_has_tess) {
      cmd_state->rp.has_tess = true;
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindPipeline(VkCommandBuffer commandBuffer,
                   VkPipelineBindPoint pipelineBindPoint,
                   VkPipeline _pipeline)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_pipeline, pipeline, _pipeline);

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
      cmd->state.shaders[MESA_SHADER_COMPUTE] =
         pipeline->shaders[MESA_SHADER_COMPUTE];
      tu_cs_emit_state_ib(&cmd->cs,
                          pipeline->shaders[MESA_SHADER_COMPUTE]->state);
      cmd->state.compute_load_state = pipeline->load_state;
      return;
   }

   assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);

   struct tu_graphics_pipeline *gfx_pipeline = tu_pipeline_to_graphics(pipeline);
   cmd->state.dirty |= TU_CMD_DIRTY_DESC_SETS | TU_CMD_DIRTY_SHADER_CONSTS |
                       TU_CMD_DIRTY_VS_PARAMS | TU_CMD_DIRTY_PROGRAM;

   tu_bind_vs(cmd, pipeline->shaders[MESA_SHADER_VERTEX]);
   tu_bind_tcs(cmd, pipeline->shaders[MESA_SHADER_TESS_CTRL]);
   tu_bind_tes(cmd, pipeline->shaders[MESA_SHADER_TESS_EVAL]);
   tu_bind_gs(cmd, pipeline->shaders[MESA_SHADER_GEOMETRY]);
   tu_bind_fs(cmd, pipeline->shaders[MESA_SHADER_FRAGMENT]);

   /* We precompile static state and count it as dynamic, so we have to
    * manually clear bitset that tells which dynamic state is set, in order to
    * make sure that future dynamic state will be emitted. The issue is that
    * framework remembers only a past REAL dynamic state and compares a new
    * dynamic state against it, and not against our static state masquaraded
    * as dynamic.
    */
   BITSET_ANDNOT(cmd->vk.dynamic_graphics_state.set,
                 cmd->vk.dynamic_graphics_state.set,
                 pipeline->static_state_mask);

   vk_cmd_set_dynamic_graphics_state(&cmd->vk,
                                     &gfx_pipeline->dynamic_state);
   cmd->state.program = pipeline->program;

   cmd->state.load_state = pipeline->load_state;
   cmd->state.prim_order_gmem = pipeline->prim_order.state_gmem;
   cmd->state.pipeline_sysmem_single_prim_mode = pipeline->prim_order.sysmem_single_prim_mode;
   cmd->state.pipeline_has_tess = pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
   cmd->state.pipeline_disable_gmem = gfx_pipeline->feedback_loop_may_involve_textures;

   tu_pipeline_update_rp_state(&cmd->state);

   if (pipeline->lrz_blend.valid) {
      if (cmd->state.lrz_blend_status !=
          pipeline->lrz_blend.lrz_blend_status) {
         cmd->state.lrz_blend_status = pipeline->lrz_blend.lrz_blend_status;
         cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
      }
   }
   cmd->state.pipeline_blend_lrz = pipeline->lrz_blend.valid;

   if (pipeline->disable_fs.valid) {
      if (cmd->state.disable_fs != pipeline->disable_fs.disable_fs) {
         cmd->state.disable_fs = pipeline->disable_fs.disable_fs;
         cmd->state.dirty |= TU_CMD_DIRTY_DISABLE_FS;
      }
   }
   cmd->state.pipeline_disable_fs = pipeline->disable_fs.valid;

   if (pipeline->bandwidth.valid)
      cmd->state.bandwidth = pipeline->bandwidth;
   cmd->state.pipeline_bandwidth = pipeline->bandwidth.valid;

   struct tu_cs *cs = &cmd->draw_cs;

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (!(cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      uint32_t mask = pipeline->set_state_mask;

      tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (10 + util_bitcount(mask)));
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_CONFIG, pipeline->program.config_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS, pipeline->program.vs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_BINNING, pipeline->program.vs_binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_HS, pipeline->program.hs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DS, pipeline->program.ds_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_GS, pipeline->program.gs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_GS_BINNING, pipeline->program.gs_binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_FS, pipeline->program.fs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VPC, pipeline->program.vpc_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PRIM_MODE_GMEM, pipeline->prim_order.state_gmem);

      u_foreach_bit(i, mask)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i, pipeline->dynamic_state[i]);
   }

   cmd->state.pipeline_draw_states = pipeline->set_state_mask;
   u_foreach_bit(i, pipeline->set_state_mask)
      cmd->state.dynamic_state[i] = pipeline->dynamic_state[i];

   if (pipeline->shaders[MESA_SHADER_FRAGMENT]->fs.has_fdm !=
       cmd->state.has_fdm) {
      cmd->state.dirty |= TU_CMD_DIRTY_FDM;
      cmd->state.has_fdm =
         pipeline->shaders[MESA_SHADER_FRAGMENT]->fs.has_fdm;
   }

   if (pipeline->program.per_layer_viewport != cmd->state.per_layer_viewport ||
       pipeline->shaders[MESA_SHADER_FRAGMENT]->fs.max_fdm_layers !=
       cmd->state.max_fdm_layers) {
      cmd->state.per_layer_viewport = pipeline->program.per_layer_viewport;
      cmd->state.max_fdm_layers =
         pipeline->shaders[MESA_SHADER_FRAGMENT]->fs.max_fdm_layers;
      cmd->state.dirty |= TU_CMD_DIRTY_FDM;
   }

   if (pipeline->program.per_view_viewport != cmd->state.per_view_viewport ||
       pipeline->program.fake_single_viewport != cmd->state.fake_single_viewport) {
      cmd->state.per_view_viewport = pipeline->program.per_view_viewport;
      cmd->state.fake_single_viewport =
         pipeline->program.fake_single_viewport;
      cmd->state.dirty |= TU_CMD_DIRTY_PER_VIEW_VIEWPORT;
   }

   if (gfx_pipeline->feedback_loops != cmd->state.pipeline_feedback_loops) {
      cmd->state.pipeline_feedback_loops = gfx_pipeline->feedback_loops;
      cmd->state.dirty |= TU_CMD_DIRTY_FEEDBACK_LOOPS | TU_CMD_DIRTY_LRZ;
   }

   if (pipeline->program.writes_shading_rate !=
          cmd->state.pipeline_writes_shading_rate ||
       pipeline->program.reads_shading_rate !=
          cmd->state.pipeline_reads_shading_rate) {
      cmd->state.pipeline_writes_shading_rate =
         pipeline->program.writes_shading_rate;
      cmd->state.pipeline_reads_shading_rate =
         pipeline->program.reads_shading_rate;
      cmd->state.dirty |= TU_CMD_DIRTY_SHADING_RATE;
   }

   bool raster_order_attachment_access =
      pipeline->output.raster_order_attachment_access ||
      pipeline->ds.raster_order_attachment_access;
   if (!cmd->state.raster_order_attachment_access_valid ||
       raster_order_attachment_access !=
       cmd->state.raster_order_attachment_access) {
      cmd->state.raster_order_attachment_access =
         raster_order_attachment_access;
      cmd->state.dirty |= TU_CMD_DIRTY_RAST_ORDER;
      cmd->state.raster_order_attachment_access_valid = true;
   }
}

void
tu_flush_for_access(struct tu_cache_state *cache,
                    enum tu_cmd_access_mask src_mask,
                    enum tu_cmd_access_mask dst_mask)
{
   BITMASK_ENUM(tu_cmd_flush_bits) flush_bits = 0;

   if (src_mask & TU_ACCESS_SYSMEM_WRITE) {
      cache->pending_flush_bits |= TU_CMD_FLAG_ALL_INVALIDATE;
   }

   if (src_mask & TU_ACCESS_CP_WRITE) {
      /* Flush the CP write queue.
       */
      cache->pending_flush_bits |=
         TU_CMD_FLAG_WAIT_MEM_WRITES |
         TU_CMD_FLAG_ALL_INVALIDATE;
   }

#define SRC_FLUSH(domain, clean, invalidate) \
   if (src_mask & TU_ACCESS_##domain##_WRITE) {                      \
      cache->pending_flush_bits |= TU_CMD_FLAG_##clean |             \
         (TU_CMD_FLAG_ALL_INVALIDATE & ~TU_CMD_FLAG_##invalidate);   \
   }

   SRC_FLUSH(UCHE, CACHE_CLEAN, CACHE_INVALIDATE)
   SRC_FLUSH(CCU_COLOR, CCU_CLEAN_COLOR, CCU_INVALIDATE_COLOR)
   SRC_FLUSH(CCU_DEPTH, CCU_CLEAN_DEPTH, CCU_INVALIDATE_DEPTH)

#undef SRC_FLUSH

#define SRC_INCOHERENT_FLUSH(domain, clean, invalidate)              \
   if (src_mask & TU_ACCESS_##domain##_INCOHERENT_WRITE) {           \
      flush_bits |= TU_CMD_FLAG_##clean;                             \
      cache->pending_flush_bits |=                                   \
         (TU_CMD_FLAG_ALL_INVALIDATE & ~TU_CMD_FLAG_##invalidate);   \
   }

   SRC_INCOHERENT_FLUSH(CCU_COLOR, CCU_CLEAN_COLOR, CCU_INVALIDATE_COLOR)
   SRC_INCOHERENT_FLUSH(CCU_DEPTH, CCU_CLEAN_DEPTH, CCU_INVALIDATE_DEPTH)
   SRC_INCOHERENT_FLUSH(UCHE, CACHE_CLEAN, CACHE_INVALIDATE)

#undef SRC_INCOHERENT_FLUSH

   /* Treat host & sysmem write accesses the same, since the kernel implicitly
    * drains the queue before signalling completion to the host.
    */
   if (dst_mask & (TU_ACCESS_SYSMEM_READ | TU_ACCESS_SYSMEM_WRITE)) {
      flush_bits |= cache->pending_flush_bits & TU_CMD_FLAG_ALL_CLEAN;
   }

#define DST_FLUSH(domain, clean, invalidate) \
   if (dst_mask & (TU_ACCESS_##domain##_READ |                 \
                   TU_ACCESS_##domain##_WRITE)) {              \
      flush_bits |= cache->pending_flush_bits &                \
         (TU_CMD_FLAG_##invalidate |                           \
          (TU_CMD_FLAG_ALL_CLEAN & ~TU_CMD_FLAG_##clean));     \
   }

   DST_FLUSH(UCHE, CACHE_CLEAN, CACHE_INVALIDATE)
   DST_FLUSH(CCU_COLOR, CCU_CLEAN_COLOR, CCU_INVALIDATE_COLOR)
   DST_FLUSH(CCU_DEPTH, CCU_CLEAN_DEPTH, CCU_INVALIDATE_DEPTH)

#undef DST_FLUSH

#define DST_INCOHERENT_FLUSH(domain, flush, invalidate) \
   if (dst_mask & (TU_ACCESS_##domain##_INCOHERENT_READ |      \
                   TU_ACCESS_##domain##_INCOHERENT_WRITE)) {   \
      flush_bits |= TU_CMD_FLAG_##invalidate |                 \
          (cache->pending_flush_bits &                         \
           (TU_CMD_FLAG_ALL_CLEAN & ~TU_CMD_FLAG_##flush));    \
   }

   DST_INCOHERENT_FLUSH(CCU_COLOR, CCU_CLEAN_COLOR, CCU_INVALIDATE_COLOR)
   DST_INCOHERENT_FLUSH(CCU_DEPTH, CCU_CLEAN_DEPTH, CCU_INVALIDATE_DEPTH)
   DST_INCOHERENT_FLUSH(UCHE, CACHE_CLEAN, CACHE_INVALIDATE)

   if (dst_mask & TU_ACCESS_BINDLESS_DESCRIPTOR_READ) {
      flush_bits |= TU_CMD_FLAG_BINDLESS_DESCRIPTOR_INVALIDATE;
   }

   /* There are multiple incoherent copies of CCHE, so any read through it may
    * require invalidating it and we cannot optimize away invalidates.
    */
   if (dst_mask & TU_ACCESS_CCHE_READ) {
      flush_bits |= TU_CMD_FLAG_CCHE_INVALIDATE;
   }

   /* The blit cache is a special case dependency between CP_EVENT_WRITE::BLIT
    * (from GMEM loads/clears) to any GMEM attachment reads done via the UCHE
    * (Eg: Input attachments/CP_BLIT) which needs an explicit BLIT_CACHE_CLEAN
    * for the event blit writes to land, it has the following properties:
    * - Set on reads rather than on writes, like flushes.
    * - Not executed automatically if pending, like invalidates.
    * - Pending bits passed through to secondary command buffers, if they're
    *   continuing the render pass.
    */
   if (src_mask & TU_ACCESS_BLIT_WRITE_GMEM) {
      cache->pending_flush_bits |= TU_CMD_FLAG_BLIT_CACHE_CLEAN;
   }

   if ((dst_mask & TU_ACCESS_UCHE_READ_GMEM) &&
       (cache->pending_flush_bits & TU_CMD_FLAG_BLIT_CACHE_CLEAN)) {
      flush_bits |= TU_CMD_FLAG_BLIT_CACHE_CLEAN;
   }

   /* Nothing writes through the RTU cache so there's no point trying to
    * optimize this. Just always invalidate.
    */
   if (dst_mask & TU_ACCESS_RTU_READ)
      flush_bits |= TU_CMD_FLAG_RTU_INVALIDATE;

#undef DST_INCOHERENT_FLUSH

   cache->flush_bits |= flush_bits;
   cache->pending_flush_bits &= ~flush_bits;
}

/* When translating Vulkan access flags to which cache is accessed
 * (CCU/UCHE/sysmem), we should take into account both the access flags and
 * the stage so that accesses with MEMORY_READ_BIT/MEMORY_WRITE_BIT + a
 * specific stage return something sensible. The specification for
 * VK_KHR_synchronization2 says that we should do this:
 *
 *    Additionally, scoping the pipeline stages into the barrier structs
 *    allows the use of the MEMORY_READ and MEMORY_WRITE flags without
 *    sacrificing precision. The per-stage access flags should be used to
 *    disambiguate specific accesses in a given stage or set of stages - for
 *    instance, between uniform reads and sampling operations.
 *
 * Note that while in all known cases the stage is actually enough, we should
 * still narrow things down based on the access flags to handle "old-style"
 * barriers that may specify a wider range of stages but more precise access
 * flags. These helpers allow us to do both.
 */

static bool
filter_read_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                   VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return (flags & (tu_flags | VK_ACCESS_2_MEMORY_READ_BIT)) &&
      (stages & (tu_stages | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
}

static bool
filter_write_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                    VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return (flags & (tu_flags | VK_ACCESS_2_MEMORY_WRITE_BIT)) &&
      (stages & (tu_stages | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
}

static bool
gfx_read_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return filter_read_access(flags, stages, tu_flags,
                             tu_stages | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}

static bool
gfx_write_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                 VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return filter_write_access(flags, stages, tu_flags,
                              tu_stages | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}

static enum tu_cmd_access_mask
vk2tu_access(VkAccessFlags2 flags, VkAccessFlags3KHR flags2,
             VkPipelineStageFlags2 stages, bool image_only, bool gmem,
             bool sparse_aliasing)
{
   BITMASK_ENUM(tu_cmd_access_mask) mask = 0;

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                       VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT |
                       VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
                       VK_ACCESS_2_HOST_READ_BIT,
                       VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                       VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT |
                       VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT |
                       VK_PIPELINE_STAGE_2_HOST_BIT))
      mask |= TU_ACCESS_SYSMEM_READ;

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
                        VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT))
      mask |= TU_ACCESS_CP_WRITE;

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_HOST_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT))
      mask |= TU_ACCESS_SYSMEM_WRITE;

#define SHADER_STAGES \
   (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT | \
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)


   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_INDEX_READ_BIT |
                       VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                       VK_ACCESS_2_UNIFORM_READ_BIT |
                       VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT |
                       VK_ACCESS_2_SHADER_READ_BIT |
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
                       VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                       VK_ACCESS_2_SHADER_BINDING_TABLE_READ_BIT_KHR |
                       VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                       VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                       VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                       VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
                       VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                       VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR |
                       SHADER_STAGES)) {
      if (sparse_aliasing)
         mask |= TU_ACCESS_UCHE_INCOHERENT_READ;
      else
         mask |= TU_ACCESS_UCHE_READ;
      mask |= TU_ACCESS_CCHE_READ;
   }

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                       SHADER_STAGES))
       mask |= TU_ACCESS_UCHE_READ | TU_ACCESS_CCHE_READ | TU_ACCESS_RTU_READ;

   /* Reading the AS for copying involves doing CmdDispatchIndirect with the
    * copy size as a parameter, so it's read by the CP as well as a shader.
    */
   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                       VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                       VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR))
       mask |= TU_ACCESS_SYSMEM_READ | TU_ACCESS_UCHE_READ |
          TU_ACCESS_CCHE_READ;


   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT,
                       SHADER_STAGES)) {
       mask |= TU_ACCESS_UCHE_READ_GMEM;
       if (sparse_aliasing)
          mask |= TU_ACCESS_UCHE_INCOHERENT_READ;
   }

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_DESCRIPTOR_BUFFER_READ_BIT_EXT,
                       SHADER_STAGES)) {
      if (sparse_aliasing)
         mask |= TU_ACCESS_UCHE_INCOHERENT_READ;
      else
         mask |= TU_ACCESS_UCHE_READ;
      mask |= TU_ACCESS_BINDLESS_DESCRIPTOR_READ |
              TU_ACCESS_CCHE_READ;
   }

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_SHADER_WRITE_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                        VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
                        VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT |
                        SHADER_STAGES)) {
      if (sparse_aliasing)
         mask |= TU_ACCESS_UCHE_INCOHERENT_WRITE;
      else
         mask |= TU_ACCESS_UCHE_WRITE;
   }

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR))
       mask |= TU_ACCESS_UCHE_WRITE | TU_ACCESS_CP_WRITE;

   /* When using GMEM, the CCU is always flushed automatically to GMEM, and
    * then GMEM is flushed to sysmem. Furthermore, we already had to flush any
    * previous writes in sysmem mode when transitioning to GMEM. Therefore we
    * can ignore CCU and pretend that color attachments and transfers use
    * sysmem directly.
    */

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                       VK_ACCESS_2_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      if (gmem)
         mask |= TU_ACCESS_SYSMEM_READ;
      else
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_READ;
   }

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                       VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) {
      if (gmem)
         mask |= TU_ACCESS_SYSMEM_READ;
      else
         mask |= TU_ACCESS_CCU_DEPTH_INCOHERENT_READ;
   }

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
      }
   }

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;
      }
   }

   if (filter_write_access(flags, stages,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_COPY_BIT |
                           VK_PIPELINE_STAGE_2_BLIT_BIT |
                           VK_PIPELINE_STAGE_2_CLEAR_BIT |
                           VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                           VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else if (image_only && !sparse_aliasing) {
         /* Because we always split up blits/copies of images involving
          * multiple layers, we always access each layer in the same way, with
          * the same base address, same format, etc. This means we can avoid
          * flushing between multiple writes to the same image. This elides
          * flushes between e.g. multiple blits to the same image.
          */
         mask |= TU_ACCESS_CCU_COLOR_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
      }
   }

   if (filter_read_access(flags, stages,
                          VK_ACCESS_2_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_2_COPY_BIT |
                          VK_PIPELINE_STAGE_2_BLIT_BIT |
                          VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT)) {
      if (sparse_aliasing)
         mask |= TU_ACCESS_UCHE_INCOHERENT_READ;
      else
         mask |= TU_ACCESS_UCHE_READ;
      mask |= TU_ACCESS_CCHE_READ;
   }

   return mask;
}

/* These helpers deal with legacy BOTTOM_OF_PIPE/TOP_OF_PIPE stages.
 */

static VkPipelineStageFlags2
sanitize_src_stage(VkPipelineStageFlags2 stage_mask)
{
   /* From the Vulkan spec:
    *
    *    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT is ...  equivalent to
    *    VK_PIPELINE_STAGE_2_NONE in the first scope.
    *
    *    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT is equivalent to
    *    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT with VkAccessFlags2 set to 0
    *    when specified in the first synchronization scope, ...
    */
   if (stage_mask & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT)
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

   return stage_mask & ~VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
}

static VkPipelineStageFlags2
sanitize_dst_stage(VkPipelineStageFlags2 stage_mask)
{
   /* From the Vulkan spec:
    *
    *    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT is equivalent to
    *    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT with VkAccessFlags2 set to 0
    *    when specified in the second synchronization scope, ...
    *
    *    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT is ... equivalent to
    *    VK_PIPELINE_STAGE_2_NONE in the second scope.
    *
    */
   if (stage_mask & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

   return stage_mask & ~VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
}

static enum tu_stage
vk2tu_single_stage(struct tu_device *dev,
                   VkPipelineStageFlags2 vk_stage, bool dst)
{
   /* If the destination stage is executed on the CP, then the CP also has to
    * wait for any WFI's to finish. This is already done for draw calls,
    * including before indirect param reads, for the most part, so we just
    * need to WFI and can use TU_STAGE_GPU.
    *
    * However, some indirect draw opcodes, depending on firmware, don't have
    * implicit CP_WAIT_FOR_ME so we have to handle it manually.
    *
    * Transform feedback counters are read via CP_MEM_TO_REG, which implicitly
    * does CP_WAIT_FOR_ME, so we don't include them here.
    *
    * Currently we read the draw predicate using CP_MEM_TO_MEM, which
    * also implicitly does CP_WAIT_FOR_ME. However CP_DRAW_PRED_SET does *not*
    * implicitly do CP_WAIT_FOR_ME, it seems to only wait for counters to
    * complete since it's written for DX11 where you can only predicate on the
    * result of a query object. So if we implement 64-bit comparisons in the
    * future, or if CP_DRAW_PRED_SET grows the capability to do 32-bit
    * comparisons, then this will have to be dealt with.
    */
   if (vk_stage == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT ||
       vk_stage == VK_PIPELINE_STAGE_2_FRAGMENT_DENSITY_PROCESS_BIT_EXT)
      return TU_STAGE_BV_CP;

   if (vk_stage == VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
      return dst ? TU_STAGE_BV_CP : TU_STAGE_BR;

   if (vk_stage == VK_PIPELINE_STAGE_2_HOST_BIT)
      return dst ? TU_STAGE_BOTTOM : TU_STAGE_BV_CP;

   if (dev->physical_device->info->chip >= 7) {
      if (vk_stage == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT ||
          vk_stage == VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT ||
          vk_stage == VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT) {
         return dst ? TU_STAGE_BV : TU_STAGE_BR;
      }
   }

   return TU_STAGE_BR;
}

static enum tu_stage
vk2tu_src_stage(struct tu_device *dev,
                VkPipelineStageFlags2 vk_stages)
{
   enum tu_stage stage = TU_STAGE_BV_CP;
   u_foreach_bit64 (bit, vk_stages) {
      enum tu_stage new_stage = vk2tu_single_stage(dev, 1ull << bit, false);
      stage = MAX2(stage, new_stage);
   }

   return stage;
}

static enum tu_stage
vk2tu_dst_stage(struct tu_device *dev,
                VkPipelineStageFlags2 vk_stages)
{
   enum tu_stage stage = TU_STAGE_BOTTOM;
   u_foreach_bit64 (bit, vk_stages) {
      enum tu_stage new_stage = vk2tu_single_stage(dev, 1ull << bit, true);
      stage = MIN2(stage, new_stage);
   }

   return stage;
}

static void
tu_flush_for_stage(struct tu_cache_state *cache,
                   enum tu_stage src_stage, enum tu_stage dst_stage)
{
   /* Even if the source is the host or CP, the destination access could
    * generate invalidates that we have to wait to complete.
    */
   if (src_stage < TU_STAGE_BR &&
       (cache->flush_bits & TU_CMD_FLAG_ALL_INVALIDATE))
      src_stage = TU_STAGE_BR;

   if (src_stage >= dst_stage) {
      cache->flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
      if (dst_stage <= TU_STAGE_BV) {
         cache->flush_bits |= TU_CMD_FLAG_WAIT_FOR_BR;
         if (dst_stage == TU_STAGE_BV_CP)
            cache->pending_flush_bits |= TU_CMD_FLAG_WAIT_FOR_ME;
      }
   }
}

void
tu_render_pass_state_merge(struct tu_render_pass_state *dst,
                           const struct tu_render_pass_state *src)
{
   dst->xfb_used |= src->xfb_used;
   dst->has_tess |= src->has_tess;
   dst->has_prim_generated_query_in_rp |= src->has_prim_generated_query_in_rp;
   dst->has_vtx_stats_query_in_rp |= src->has_vtx_stats_query_in_rp;
   dst->has_zpass_done_sample_count_write_in_rp |= src->has_zpass_done_sample_count_write_in_rp;
   dst->disable_gmem |= src->disable_gmem;
   dst->sysmem_single_prim_mode |= src->sysmem_single_prim_mode;
   dst->draw_cs_writes_to_cond_pred |= src->draw_cs_writes_to_cond_pred;
   dst->shared_viewport |= src->shared_viewport;

   dst->drawcall_count += src->drawcall_count;
   dst->drawcall_bandwidth_per_sample_sum +=
      src->drawcall_bandwidth_per_sample_sum;
   if (!dst->lrz_disable_reason && src->lrz_disable_reason) {
      dst->lrz_disable_reason = src->lrz_disable_reason;
      dst->lrz_disabled_at_draw =
         dst->drawcall_count + src->lrz_disabled_at_draw;
   }
   if (!dst->lrz_write_disabled_at_draw &&
       src->lrz_write_disabled_at_draw) {
      dst->lrz_write_disabled_at_draw =
         dst->drawcall_count + src->lrz_write_disabled_at_draw;
   }
   if (!dst->gmem_disable_reason && src->gmem_disable_reason) {
      dst->gmem_disable_reason = src->gmem_disable_reason;
   }
}

void
tu_restore_suspended_pass(struct tu_cmd_buffer *cmd,
                          struct tu_cmd_buffer *suspended)
{
   cmd->state.pass = suspended->state.suspended_pass.pass;
   cmd->state.subpass = suspended->state.suspended_pass.subpass;
   cmd->state.framebuffer = suspended->state.suspended_pass.framebuffer;
   cmd->state.attachments = suspended->state.suspended_pass.attachments;
   cmd->state.clear_values = suspended->state.suspended_pass.clear_values;
   cmd->state.render_area = suspended->state.suspended_pass.render_area;
   cmd->state.gmem_layout = suspended->state.suspended_pass.gmem_layout;
   cmd->state.tiling = &cmd->state.framebuffer->tiling[cmd->state.gmem_layout];
   cmd->state.lrz = suspended->state.suspended_pass.lrz;
}

/* Take the saved pre-chain in "secondary" and copy its commands to "cmd",
 * appending it after any saved-up commands in "cmd".
 */
void
tu_append_pre_chain(struct tu_cmd_buffer *cmd,
                    struct tu_cmd_buffer *secondary)
{
   tu_cs_add_entries(&cmd->draw_cs, &secondary->pre_chain.draw_cs);
   tu_cs_add_entries(&cmd->draw_epilogue_cs,
                     &secondary->pre_chain.draw_epilogue_cs);

   tu_render_pass_state_merge(&cmd->state.rp,
                              &secondary->pre_chain.state);
   tu_clone_trace(cmd, &cmd->draw_cs,
                  &cmd->rp_trace, &secondary->pre_chain.rp_trace);
   util_dynarray_append_dynarray(&cmd->fdm_bin_patchpoints,
                                 &secondary->pre_chain.fdm_bin_patchpoints);

   cmd->pre_chain.fdm_offset = secondary->pre_chain.fdm_offset;
   if (secondary->pre_chain.fdm_offset) {
      memcpy(cmd->pre_chain.fdm_offsets,
             secondary->pre_chain.fdm_offsets,
             sizeof(cmd->pre_chain.fdm_offsets));
   }
}

/* Take the saved post-chain in "secondary" and copy it to "cmd".
 */
void
tu_append_post_chain(struct tu_cmd_buffer *cmd,
                     struct tu_cmd_buffer *secondary)
{
   tu_cs_add_entries(&cmd->draw_cs, &secondary->draw_cs);
   tu_cs_add_entries(&cmd->draw_epilogue_cs, &secondary->draw_epilogue_cs);

   tu_clone_trace(cmd, &cmd->draw_cs, &cmd->rp_trace, &secondary->rp_trace);
   cmd->state.rp = secondary->state.rp;
   util_dynarray_append_dynarray(&cmd->fdm_bin_patchpoints,
                                 &secondary->fdm_bin_patchpoints);
}

/* Assuming "secondary" is just a sequence of suspended and resuming passes,
 * copy its state to "cmd". This also works instead of tu_append_post_chain(),
 * but it's a bit slower because we don't assume that the chain begins in
 * "secondary" and therefore have to care about the command buffer's
 * renderpass state.
 */
void
tu_append_pre_post_chain(struct tu_cmd_buffer *cmd,
                         struct tu_cmd_buffer *secondary)
{
   tu_cs_add_entries(&cmd->draw_cs, &secondary->draw_cs);
   tu_cs_add_entries(&cmd->draw_epilogue_cs, &secondary->draw_epilogue_cs);

   tu_clone_trace(cmd, &cmd->draw_cs, &cmd->rp_trace, &secondary->rp_trace);
   tu_render_pass_state_merge(&cmd->state.rp,
                              &secondary->state.rp);
   util_dynarray_append_dynarray(&cmd->fdm_bin_patchpoints,
                                 &secondary->fdm_bin_patchpoints);
}

/* Take the current render pass state and save it to "pre_chain" to be
 * combined later.
 */
static void
tu_save_pre_chain(struct tu_cmd_buffer *cmd)
{
   tu_cs_add_entries(&cmd->pre_chain.draw_cs,
                     &cmd->draw_cs);
   tu_cs_add_entries(&cmd->pre_chain.draw_epilogue_cs,
                     &cmd->draw_epilogue_cs);
   u_trace_move(&cmd->pre_chain.rp_trace, &cmd->rp_trace);
   cmd->pre_chain.state = cmd->state.rp;
   util_dynarray_append_dynarray(&cmd->pre_chain.fdm_bin_patchpoints,
                                 &cmd->fdm_bin_patchpoints);
   cmd->pre_chain.patchpoints_ctx = cmd->patchpoints_ctx;
   cmd->patchpoints_ctx = NULL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCmdBuffers)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VkResult result;

   assert(commandBufferCount > 0);

   /* Emit any pending flushes. */
   if (cmd->state.pass) {
      tu_clean_all_pending(&cmd->state.renderpass_cache);
      TU_CALLX(cmd->device, tu_emit_cache_flush_renderpass)(cmd);
   } else {
      tu_clean_all_pending(&cmd->state.cache);
      TU_CALLX(cmd->device, tu_emit_cache_flush)(cmd);
   }

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(tu_cmd_buffer, secondary, pCmdBuffers[i]);

      if (secondary->usage_flags &
          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
         assert(tu_cs_is_empty(&secondary->cs));

         TU_CALLX(cmd->device, tu_lrz_flush_valid_during_renderpass)
            (cmd, &cmd->draw_cs);

         result = tu_cs_add_entries(&cmd->draw_cs, &secondary->draw_cs);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(&cmd->vk, result);
            break;
         }

         result = tu_cs_add_entries(&cmd->draw_epilogue_cs,
               &secondary->draw_epilogue_cs);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(&cmd->vk, result);
            break;
         }

         /* If LRZ was made invalid in secondary - we should disable
          * LRZ retroactively for the whole renderpass.
          */
         if (!secondary->state.lrz.valid)
            cmd->state.lrz.valid = false;
         if (secondary->state.lrz.gpu_dir_set)
            cmd->state.lrz.gpu_dir_set = true;
         if (cmd->state.lrz.prev_direction == TU_LRZ_UNKNOWN &&
             secondary->state.lrz.prev_direction != TU_LRZ_UNKNOWN)
            cmd->state.lrz.prev_direction =
               secondary->state.lrz.prev_direction;

         tu_clone_trace(cmd, &cmd->draw_cs, &cmd->rp_trace, &secondary->rp_trace);
         tu_render_pass_state_merge(&cmd->state.rp, &secondary->state.rp);
         util_dynarray_append_dynarray(&cmd->fdm_bin_patchpoints,
                                       &secondary->fdm_bin_patchpoints);
      } else {
         struct tu_cs *cs = &cmd->cs;

         /* If the secondary can be used multiple times, we have to set its
          * patchpoints on the GPU. Set them here, and create a new
          * patchpoint pointing to the CP_MEM_WRITE packet. Otherwise just
          * copy them over adjusting the index.
          */
         bool simultaneous_use = secondary->usage_flags &
             VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

         /* If this cmdbuf itself can be used multiple times in a submit then
          * its patchpoint will also be updated on the GPU.
          */
         if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
            tu_cs_set_writeable(cs, true);

         util_dynarray_foreach (&secondary->vis_stream_patchpoints,
                                struct tu_vis_stream_patchpoint,
                                secondary_patchpoint) {
            struct tu_vis_stream_patchpoint patchpoint =
               *secondary_patchpoint;
            patchpoint.render_pass_idx += cmd->state.tile_render_pass_count;

            if (simultaneous_use) {
               tu_cs_reserve_space(cs, 5);
               tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
               tu_cs_emit_qw(cs, patchpoint.iova);
               patchpoint.iova = tu_cs_get_cur_iova(cs);
               patchpoint.data = cs->cur;
               tu_cs_emit_qw(cs, 0);
            }

            util_dynarray_append(&cmd->vis_stream_patchpoints,
                                 patchpoint);
         }

         if (cmd->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
            tu_cs_set_writeable(cs, false);

         if (simultaneous_use) {
            tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
            tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

            /* Make BV wait for updates on BR to land */
            if (cmd->device->physical_device->info->chip >= 7) {
               tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
               tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BR) |
                              CP_THREAD_CONTROL_0_SYNC_THREADS);
            }
         }

         cmd->state.tile_render_pass_count +=
            secondary->state.tile_render_pass_count;
         cmd->vsc_size = MAX2(cmd->vsc_size, secondary->vsc_size);

         switch (secondary->state.suspend_resume) {
         case SR_NONE:
            assert(tu_cs_is_empty(&secondary->draw_cs));
            assert(tu_cs_is_empty(&secondary->draw_epilogue_cs));
            tu_cs_add_entries(&cmd->cs, &secondary->cs);
            tu_clone_trace(cmd, &cmd->cs, &cmd->trace, &secondary->trace);
            break;

         case SR_IN_PRE_CHAIN:
            /* cmd may be empty, which means that the chain begins before cmd
             * in which case we have to update its state.
             */
            if (cmd->state.suspend_resume == SR_NONE) {
               cmd->state.suspend_resume = SR_IN_PRE_CHAIN;
            }

            /* The secondary is just a continuous suspend/resume chain so we
             * just have to append it to the the command buffer.
             */
            assert(tu_cs_is_empty(&secondary->cs));
            tu_append_pre_post_chain(cmd, secondary);
            break;

         case SR_AFTER_PRE_CHAIN:
         case SR_IN_CHAIN:
         case SR_IN_CHAIN_AFTER_PRE_CHAIN:
            if (secondary->state.suspend_resume == SR_AFTER_PRE_CHAIN ||
                secondary->state.suspend_resume == SR_IN_CHAIN_AFTER_PRE_CHAIN) {
               tu_append_pre_chain(cmd, secondary);

               /* We're about to render, so we need to end the command stream
                * in case there were any extra commands generated by copying
                * the trace.
                */
               tu_cs_end(&cmd->draw_cs);
               tu_cs_end(&cmd->draw_epilogue_cs);

               switch (cmd->state.suspend_resume) {
               case SR_NONE:
               case SR_IN_PRE_CHAIN:
                  /* The renderpass chain ends in the secondary but isn't
                   * started in the primary, so we have to move the state to
                   * `pre_chain`.
                   */
                  tu_save_pre_chain(cmd);
                  cmd->state.suspend_resume = SR_AFTER_PRE_CHAIN;
                  break;
               case SR_IN_CHAIN:
               case SR_IN_CHAIN_AFTER_PRE_CHAIN: {
                  /* The renderpass ends in the secondary and starts somewhere
                   * earlier in this primary. Since the last render pass in
                   * the chain is in the secondary, we are technically outside
                   * of a render pass.  Fix that here by reusing the dynamic
                   * render pass that was setup for the last suspended render
                   * pass before the secondary.
                   */
                  tu_restore_suspended_pass(cmd, cmd);

                  const struct VkOffset2D *fdm_offsets =
                     cmd->pre_chain.fdm_offset ?
                     cmd->pre_chain.fdm_offsets : NULL;
                  TU_CALLX(cmd->device, tu_cmd_render)(cmd, fdm_offsets);
                  if (cmd->state.suspend_resume == SR_IN_CHAIN)
                     cmd->state.suspend_resume = SR_NONE;
                  else
                     cmd->state.suspend_resume = SR_AFTER_PRE_CHAIN;
                  break;
               }
               case SR_AFTER_PRE_CHAIN:
                  UNREACHABLE("resuming render pass is not preceded by suspending one");
               }

               tu_reset_render_pass(cmd);
            }

            tu_cs_add_entries(&cmd->cs, &secondary->cs);

            if (secondary->state.suspend_resume == SR_IN_CHAIN_AFTER_PRE_CHAIN ||
                secondary->state.suspend_resume == SR_IN_CHAIN) {
               /* The secondary ends in a "post-chain" (the opposite of a
                * pre-chain) that we need to copy into the current command
                * buffer.
                */
               tu_append_post_chain(cmd, secondary);
               cmd->state.suspended_pass = secondary->state.suspended_pass;

               switch (cmd->state.suspend_resume) {
               case SR_NONE:
                  cmd->state.suspend_resume = SR_IN_CHAIN;
                  break;
               case SR_AFTER_PRE_CHAIN:
                  cmd->state.suspend_resume = SR_IN_CHAIN_AFTER_PRE_CHAIN;
                  break;
               default:
                  UNREACHABLE("suspending render pass is followed by a not resuming one");
               }
            }
         }

         cmd->state.total_renderpasses += secondary->state.total_renderpasses;
         cmd->state.total_dispatches += secondary->state.total_dispatches;
      }

      cmd->state.index_size = secondary->state.index_size; /* for restart index update */
   }
   cmd->state.dirty = ~0u; /* TODO: set dirty only what needs to be */

   if (!cmd->state.lrz.gpu_dir_tracking && cmd->state.pass) {
      /* After a secondary command buffer is executed, LRZ is not valid
       * until it is cleared again.
       */
      cmd->state.lrz.valid = false;
   }

   /* After executing secondary command buffers, there may have been arbitrary
    * flushes executed, so when we encounter a pipeline barrier with a
    * srcMask, we have to assume that we need to invalidate. Therefore we need
    * to re-initialize the cache with all pending invalidate bits set.
    */
   if (cmd->state.pass) {
      struct tu_cache_state *cache = &cmd->state.renderpass_cache;
      BITMASK_ENUM(tu_cmd_flush_bits) retained_pending_flush_bits =
         cache->pending_flush_bits & TU_CMD_FLAG_BLIT_CACHE_CLEAN;
      tu_cache_init(cache);
      cache->pending_flush_bits |= retained_pending_flush_bits;
   } else {
      tu_cache_init(&cmd->state.cache);
   }
}

static void
tu_subpass_barrier(struct tu_cmd_buffer *cmd_buffer,
                   const struct tu_subpass_barrier *barrier,
                   bool external)
{
   /* Note: we don't know until the end of the subpass whether we'll use
    * sysmem, so assume sysmem here to be safe.
    */
   struct tu_cache_state *cache =
      external ? &cmd_buffer->state.cache : &cmd_buffer->state.renderpass_cache;
   VkPipelineStageFlags2 src_stage_vk =
      sanitize_src_stage(barrier->src_stage_mask);
   VkPipelineStageFlags2 dst_stage_vk =
      sanitize_dst_stage(barrier->dst_stage_mask);
   BITMASK_ENUM(tu_cmd_access_mask) src_flags =
      vk2tu_access(barrier->src_access_mask, barrier->src_access_mask2,
                   src_stage_vk, false, false,
                   cmd_buffer->device->vk.enabled_features.sparseResidencyAliased);
   BITMASK_ENUM(tu_cmd_access_mask) dst_flags =
      vk2tu_access(barrier->dst_access_mask, barrier->dst_access_mask2,
                   dst_stage_vk, false, false,
                   cmd_buffer->device->vk.enabled_features.sparseResidencyAliased);

   if (barrier->incoherent_ccu_color)
      src_flags |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
   if (barrier->incoherent_ccu_depth)
      src_flags |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;

   tu_flush_for_access(cache, src_flags, dst_flags);

   enum tu_stage src_stage = vk2tu_src_stage(cmd_buffer->device, src_stage_vk);
   enum tu_stage dst_stage = vk2tu_dst_stage(cmd_buffer->device, dst_stage_vk);
   tu_flush_for_stage(cache, src_stage, dst_stage);
}

template <chip CHIP>
static void
tu_emit_subpass_begin_gmem(struct tu_cmd_buffer *cmd, struct tu_resolve_group *resolve_group)
{
   struct tu_cs *cs = &cmd->draw_cs;
   const struct tu_subpass *subpass = cmd->state.subpass;
   uint32_t subpass_idx = subpass - cmd->state.pass->subpasses;
   const struct tu_vsc_config *vsc = tu_vsc_config(cmd, cmd->state.tiling);

   /* Shader resolve subpasses don't use GMEM */
   if (subpass->custom_resolve)
      return;

   /* If we might choose to bin, then put the loads under a check for geometry
    * having been binned to this tile.  If we don't choose to bin in the end,
    * then we will have manually set those registers to say geometry is present.
    *
    * However, if the draw CS has a write to the condition for some other reason
    * (perf queries), then we can't do this optimization since the
    * start-of-the-CS geometry condition will have been overwritten.
    */
   bool cond_load_allowed = vsc->binning &&
                            cmd->state.pass->has_cond_load_store &&
                            !cmd->state.rp.draw_cs_writes_to_cond_pred;

   if (cmd->state.pass->has_fdm)
      tu_cs_set_writeable(cs, true);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   /* This appears to be necessary when stores are followed by loads to the
    * same memory in GMEM, to prevent the loads from starting before the
    * stores have completed. See 
    * dEQP-VK.pipeline.monolithic.multisample.multisampled_render_to_single_sampled.input_attachments.initialize.r8g8b8a8_unorm_r16g16b16a16_sfloat_r16g16b16a16_sint_d16_unorm.2x.ds_resolve_sample_zero.whole_framebuffer
    * for a testcase.
    *
    * TODO: why is this not necessary between the end of one tile and the
    * start of another?
    */
   if (subpass_idx != 0) {
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_BLIT_CACHE);
   }

   /* Emit gmem loads that are first used in this subpass. */
   bool emitted_scissor = false;
   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i) {
      struct tu_render_pass_attachment *att = &cmd->state.pass->attachments[i];
      if ((att->load || att->load_stencil) && att->first_subpass_idx == subpass_idx) {
         if (!emitted_scissor) {
            tu6_emit_blit_scissor(cmd, cs, true, false);
            emitted_scissor = true;
         }
         tu_load_gmem_attachment<CHIP>(cmd, cs, resolve_group, i, i,
                                       cond_load_allowed, false);
      }
   }


   /* Emit unresolves that replicate single-sampled attachments into
    * multisampled GMEM attachments.
    */
   for (uint32_t i = 0; i < cmd->state.subpass->unresolve_count; ++i) {
      uint32_t a = cmd->state.subpass->unresolve_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      uint32_t gmem_a =
         tu_subpass_get_attachment_to_unresolve(cmd->state.subpass, i);

      tu_load_gmem_attachment<CHIP>(cmd, cs, resolve_group, a, gmem_a,
                                    cond_load_allowed, true);
   }

   if (!cmd->device->physical_device->info->props.has_generic_clear) {
      /* Emit gmem clears that are first used in this subpass. */
      emitted_scissor = false;
      for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i) {
         struct tu_render_pass_attachment *att =
            &cmd->state.pass->attachments[i];
         if (att->clear_mask && att->first_subpass_idx == subpass_idx) {
            if (!emitted_scissor) {
               tu6_emit_blit_scissor(cmd, cs, false, false);
               emitted_scissor = true;
            }
            tu_clear_gmem_attachment<CHIP>(cmd, cs, resolve_group, i);
         }
      }
   }

   tu_cond_exec_end(cs); /* CP_COND_EXEC_0_RENDER_MODE_GMEM */

   if (cmd->state.pass->has_fdm)
      tu_cs_set_writeable(cs, false);

}

/* Emits sysmem clears that are first used in this subpass. */
template <chip CHIP>
static void
tu_emit_subpass_begin_sysmem(struct tu_cmd_buffer *cmd)
{
   if (cmd->device->physical_device->info->props.has_generic_clear &&
       !cmd->state.subpass->unresolve_count)
      return;

   struct tu_cs *cs = &cmd->draw_cs;
   const struct tu_subpass *subpass = cmd->state.subpass;
   uint32_t subpass_idx = subpass - cmd->state.pass->subpasses;

   if (!subpass->custom_resolve)
      tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);

   tu6_emit_sysmem_unresolves<CHIP>(cmd, cs, cmd->state.subpass);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i) {
      struct tu_render_pass_attachment *att = &cmd->state.pass->attachments[i];
      if (att->clear_mask && att->first_subpass_idx == subpass_idx)
         tu_clear_sysmem_attachment<CHIP>(cmd, cs, i);
   }

   if (!subpass->custom_resolve)
      tu_cond_exec_end(cs); /* sysmem */
}

static void
tu7_emit_subpass_clear(struct tu_cmd_buffer *cmd, struct tu_resolve_group *resolve_group)
{
   if (cmd->state.render_area.extent.width == 0 ||
       cmd->state.render_area.extent.height == 0)
      return;

   struct tu_cs *cs = &cmd->draw_cs;
   uint32_t subpass_idx = cmd->state.subpass - cmd->state.pass->subpasses;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_GMEM |
                          CP_COND_REG_EXEC_0_SYSMEM);

   bool emitted_scissor = false;
   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i) {
      struct tu_render_pass_attachment *att =
         &cmd->state.pass->attachments[i];
      if (att->clear_mask && att->first_subpass_idx == subpass_idx) {
         if (!emitted_scissor) {
            tu6_emit_blit_scissor(cmd, cs, false, true);
            emitted_scissor = true;
         }
         tu7_generic_clear_attachment(cmd, cs, resolve_group, i);
      }
   }

   tu_cond_exec_end(cs);
}

template <chip CHIP>
static void
tu7_emit_subpass_shading_rate(struct tu_cmd_buffer *cmd,
                              const struct tu_subpass *subpass,
                              struct tu_cs *cs)
{
   if (subpass->fsr_attachment == VK_ATTACHMENT_UNUSED) {
      tu_cs_emit_regs(cs, GRAS_QUALITY_BUFFER_INFO(CHIP),
                      GRAS_QUALITY_BUFFER_DIMENSION(CHIP));
      tu_cs_emit_regs(cs, GRAS_QUALITY_BUFFER_PITCH(CHIP));
      tu_cs_emit_regs(cs, GRAS_QUALITY_BUFFER_BASE(CHIP));
      /* We need to invalidate cache when changing to NULL FSR attachment, but
       * only once.
       */
      if (!cmd->prev_fsr_is_null) {
         tu_emit_raw_event_write<A7XX>(cmd, cs, LRZ_Q_CACHE_INVALIDATE,
                                       false);
         cmd->prev_fsr_is_null = true;
      }
      return;
   }

   const struct tu_image_view *iview =
      cmd->state.attachments[subpass->fsr_attachment];
   assert(iview->vk.format == VK_FORMAT_R8_UINT);

   tu_cs_emit_regs(
      cs,
      GRAS_QUALITY_BUFFER_INFO(
         CHIP, .layered = true,
         .tile_mode = (a6xx_tile_mode) iview->image->layout[0].tile_mode, ),
      GRAS_QUALITY_BUFFER_DIMENSION(CHIP, .width = iview->view.width,
                                    .height = iview->view.height));
   tu_cs_emit_regs(
      cs, GRAS_QUALITY_BUFFER_PITCH(CHIP, .pitch = iview->view.pitch,
                                    .array_pitch = iview->view.layer_size));
   tu_cs_emit_regs(
      cs, GRAS_QUALITY_BUFFER_BASE(CHIP, .qword = iview->view.base_addr));

   tu_emit_raw_event_write<A7XX>(cmd, cs, LRZ_Q_CACHE_INVALIDATE, false);
   cmd->prev_fsr_is_null = false;
}

/* If this is a shader resolve subpass, switch to writing to sysmem.
 */
template <chip CHIP>
static void
tu_emit_subpass_custom_resolve(struct tu_cmd_buffer *cmd)
{
   struct tu_cs *cs = &cmd->draw_cs;
   const struct tu_subpass *subpass = cmd->state.subpass;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_tiling_config *tiling = cmd->state.tiling;

   if (!subpass->custom_resolve)
      return;

   trace_start_custom_resolve(&cmd->rp_trace, cs, cmd);

   /* Since a7xx, buffer location can be controlled per-buffer. We also have
    * to update the steering register so that generic clears use sysmem.
    */
   if (CHIP >= A7XX) {
      tu7_emit_sysmem_render_begin_regs<CHIP>(cmd, cs);

      /* Disable foveation offset here. It's not necessary for custom resolve.
       */
      tu_cs_emit_regs(cs, GRAS_BIN_FOVEAT(CHIP));
      tu_cs_emit_regs(cs, RB_BIN_FOVEAT(CHIP));
   } else {
      tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

      /* On a6xx the location is set in *_BIN_CONTROL */
      tu6_emit_bin_size_gmem<CHIP>(cmd, cs, BUFFERS_IN_SYSMEM, false);

      tu_cond_exec_end(cs);
   }

   /* With FDM and non-subsampled images, we switch from rendering space to
    * framebuffer space in the custom resolve subpass when not in the binning
    * pass because we are writing directly to the user-visible attachment. We
    * already aren't relying on the window scissor whenever FDM is enabled,
    * but it can get in the way if FDM offset is being used because it is
    * specified in rendering space, so the origin is shifted to the right and
    * down compared to the framebuffer-space bin coordinates and part of the
    * bin gets incorrectly clipped. Just disable it here by setting it to the
    * entire framebuffer. Add an extra tile size for when we are in the
    * binning pass and still using rendering space.
    */
   if (tu_enable_fdm_offset(cmd)) {
      tu6_emit_window_scissor<CHIP>(cs, 0, 0,
                                    fb->width + tiling->tile0.width - 1,
                                    fb->height + tiling->tile0.height - 1);
   }

   /* If FDM is enabled, we need to re-emit all FDM-related state. */
   if (cmd->state.pass->fragment_density_map.attachment !=
       VK_ATTACHMENT_UNUSED) {
      cmd->state.dirty |= TU_CMD_DIRTY_FDM;
   }
}

/* If the last subpass is a shader resolve pass, emit flushes after switching
 * to sysmem, similar to fixed-function 3D resolves. Our flushing code assumes
 * that when in GMEM mode CCU isn't in use so we have to flush it ourselves.
 */
template<chip CHIP>
static void
tu_emit_custom_resolve_end(struct tu_cmd_buffer *cmd)
{
   struct tu_cs *cs = &cmd->draw_cs;

   const struct tu_subpass *subpass = cmd->state.subpass;

   if (!subpass->custom_resolve)
      return;

   if (subpass->color_count)
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_COLOR);
   if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED)
      tu_emit_event_write<CHIP>(cmd, cs, FD_CCU_CLEAN_DEPTH);

   trace_end_custom_resolve(&cmd->rp_trace, cs);
}

/* emit loads, clears, and mrt/zs/msaa/ubwc state for the subpass that is
 * starting (either at vkCmdBeginRenderPass2() or vkCmdNextSubpass2())
 *
 * Clears and loads have to happen at this point, because with
 * VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT the loads may depend on the output of
 * a previous aliased attachment's store.
 */
template <chip CHIP>
static void
tu_emit_subpass_begin(struct tu_cmd_buffer *cmd)
{
   tu_fill_render_pass_state(&cmd->state.vk_rp, cmd->state.pass, cmd->state.subpass);

   struct tu_resolve_group resolve_group = {};

   tu_emit_subpass_custom_resolve<CHIP>(cmd);
   tu_emit_subpass_begin_gmem<CHIP>(cmd, &resolve_group);
   tu_emit_subpass_begin_sysmem<CHIP>(cmd);
   if (cmd->device->physical_device->info->props.has_generic_clear) {
      tu7_emit_subpass_clear(cmd, &resolve_group);
   }

   tu_emit_resolve_group<CHIP>(cmd, &cmd->draw_cs, &resolve_group);

   tu6_emit_zs<CHIP>(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_mrt<CHIP>(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_render_cntl<CHIP>(cmd, cmd->state.subpass, &cmd->draw_cs, false);

   if (CHIP >= A7XX) {
      tu7_emit_subpass_shading_rate<CHIP>(cmd, cmd->state.subpass, &cmd->draw_cs);
   }

   tu_set_input_attachments(cmd, cmd->state.subpass);

   vk_cmd_set_cb_attachment_count(&cmd->vk, cmd->state.subpass->color_count);

   cmd->state.dirty |= TU_CMD_DIRTY_SUBPASS;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                       const VkRenderPassBeginInfo *pRenderPassBegin,
                       const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (TU_DEBUG(DYNAMIC)) {
      vk_common_CmdBeginRenderPass2(commandBuffer, pRenderPassBegin,
                                    pSubpassBeginInfo);
      return;
   }

   VK_FROM_HANDLE(tu_render_pass, pass, pRenderPassBegin->renderPass);
   VK_FROM_HANDLE(tu_framebuffer, fb, pRenderPassBegin->framebuffer);

   const struct VkRenderPassAttachmentBeginInfo *pAttachmentInfo =
      vk_find_struct_const(pRenderPassBegin->pNext,
                           RENDER_PASS_ATTACHMENT_BEGIN_INFO);

   cmd->state.pass = pass;
   cmd->state.subpass = pass->subpasses;
   cmd->state.framebuffer = fb;
   cmd->state.render_area = pRenderPassBegin->renderArea;
   cmd->state.fdm_per_layer = pass->has_layered_fdm;

   if (pass->attachment_count > 0) {
      VK_MULTIALLOC(ma);
      vk_multialloc_add(&ma, &cmd->state.attachments,
                        const struct tu_image_view *, pass->attachment_count);
      vk_multialloc_add(&ma, &cmd->state.clear_values, VkClearValue,
                        pass->attachment_count);
      if (!vk_multialloc_alloc(&ma, &cmd->vk.pool->alloc,
                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)) {
         vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
         return;
      }
   }

   if (cmd->device->dbg_renderpass_stomp_cs) {
      tu_cs_emit_call(&cmd->cs, cmd->device->dbg_renderpass_stomp_cs);
   }

   for (unsigned i = 0; i < pass->user_attachment_count; i++) {
      cmd->state.attachments[i] = pAttachmentInfo ?
         tu_image_view_from_handle(pAttachmentInfo->pAttachments[i]) :
         cmd->state.framebuffer->attachments[i];
   }

   for (unsigned i = 0; i < pass->attachment_count - pass->user_attachment_count; i++) {
      /* With imageless attachments, the only attachments in the framebuffer
       * are MSRTSS attachments. Without imageless attachments, they are after
       * the user's attachments.
       */
      unsigned fb_idx = i + (pAttachmentInfo ? 0 : pass->user_attachment_count);
      cmd->state.attachments[i + pass->user_attachment_count] =
         cmd->state.framebuffer->attachments[fb_idx];
   }

   if (pass->attachment_count) {
      for (unsigned i = 0; i < MIN2(pRenderPassBegin->clearValueCount,
                                    pass->user_attachment_count); i++) {
         struct tu_render_pass_attachment *att = &pass->attachments[i];
         uint32_t idx = i;
         /* Clear values have to be remapped for MSRTSS, because they may be
          * moved to the multisample attachment.
          */
         if (att->remapped_clear_att != VK_ATTACHMENT_UNUSED)
            idx = att->remapped_clear_att;
         cmd->state.clear_values[idx] = pRenderPassBegin->pClearValues[i];
      }
   }

   tu_choose_gmem_layout(cmd);

   /* Note: because this is external, any flushes will happen before draw_cs
    * gets called. However deferred flushes could have to happen later as part
    * of the subpass.
    */
   tu_subpass_barrier(cmd, &pass->subpasses[0].start_barrier, true);
   cmd->state.renderpass_cache.pending_flush_bits =
      cmd->state.cache.pending_flush_bits;
   cmd->state.renderpass_cache.flush_bits = 0;

   if (pass->subpasses[0].feedback_invalidate) {
      cmd->state.renderpass_cache.flush_bits |=
         TU_CMD_FLAG_CACHE_INVALIDATE | TU_CMD_FLAG_BLIT_CACHE_CLEAN |
         TU_CMD_FLAG_WAIT_FOR_IDLE;
   }

   tu_lrz_begin_renderpass<CHIP>(cmd);

   tu_emit_renderpass_begin(cmd);
   tu_emit_subpass_begin<CHIP>(cmd);

   cmd->patchpoints_ctx = ralloc_context(NULL);
}
TU_GENX(tu_CmdBeginRenderPass2);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginRendering(VkCommandBuffer commandBuffer,
                     const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   tu_setup_dynamic_render_pass(cmd, pRenderingInfo);
   tu_setup_dynamic_framebuffer(cmd, pRenderingInfo);

   cmd->state.pass = &cmd->dynamic_pass;
   cmd->state.subpass = &cmd->dynamic_subpasses[0];
   cmd->state.framebuffer = &cmd->dynamic_framebuffer;
   cmd->state.render_area = pRenderingInfo->renderArea;
   cmd->state.fdm_per_layer =
      pRenderingInfo->flags & VK_RENDERING_PER_LAYER_FRAGMENT_DENSITY_BIT_VALVE;
   cmd->state.blit_cache_cleaned = false;

   cmd->state.attachments = cmd->dynamic_attachments;
   cmd->state.clear_values = cmd->dynamic_clear_values;

   for (unsigned i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      if (!pRenderingInfo->pColorAttachments[i].imageView)
         continue;
      uint32_t a = cmd->dynamic_subpasses[0].color_attachments[i].attachment;

      cmd->state.clear_values[a] =
         pRenderingInfo->pColorAttachments[i].clearValue;

      /* With MSRTSS, the user's attachment corresponds to the
       * resolve/unresolve attachment, not the color attachment. The color
       * attachment is the transient multisample attachment. However the clear
       * happens on the multisample attachment, so we don't remap the
       * clear_values assignment above.
       */
      bool msrtss = false;
      if (a >= cmd->dynamic_pass.user_attachment_count) {
         a = cmd->dynamic_pass.attachments[a].user_att;
         msrtss = true;
      }
      VK_FROM_HANDLE(tu_image_view, view,
                     pRenderingInfo->pColorAttachments[i].imageView);
      cmd->state.attachments[a] = view;

      if (cmd->dynamic_pass.subpass_count > 1) {
         a = cmd->dynamic_subpasses[1].color_attachments[i].attachment;
      } else {
         a = cmd->dynamic_subpasses[0].resolve_attachments[i].attachment;
      }

      if (!msrtss && a != VK_ATTACHMENT_UNUSED) {
         VK_FROM_HANDLE(tu_image_view, resolve_view,
                        pRenderingInfo->pColorAttachments[i].resolveImageView);
         cmd->state.attachments[a] = resolve_view;
      }
   }

   uint32_t a = cmd->dynamic_subpasses[0].depth_stencil_attachment.attachment;
   if (pRenderingInfo->pDepthAttachment || pRenderingInfo->pStencilAttachment) {
      const struct VkRenderingAttachmentInfo *common_info =
         (pRenderingInfo->pDepthAttachment &&
          pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) ?
         pRenderingInfo->pDepthAttachment :
         pRenderingInfo->pStencilAttachment;
      if (common_info && common_info->imageView != VK_NULL_HANDLE) {
         VK_FROM_HANDLE(tu_image_view, view, common_info->imageView);
         if (pRenderingInfo->pDepthAttachment) {
            cmd->state.clear_values[a].depthStencil.depth =
               pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth;
         }

         if (pRenderingInfo->pStencilAttachment) {
            cmd->state.clear_values[a].depthStencil.stencil =
               pRenderingInfo->pStencilAttachment->clearValue.depthStencil.stencil;
         }

         bool msrtss = false;
         if (a >= cmd->dynamic_pass.user_attachment_count) {
            a = cmd->dynamic_pass.attachments[a].user_att;
            msrtss = true;
         }

         cmd->state.attachments[a] = view;

         if (!msrtss && cmd->dynamic_subpasses[0].resolve_count >
             cmd->dynamic_subpasses[0].color_count) {
            VK_FROM_HANDLE(tu_image_view, resolve_view,
                           common_info->resolveImageView);
            a = cmd->dynamic_subpasses[0].resolve_attachments[cmd->dynamic_subpasses[0].color_count].attachment;
            cmd->state.attachments[a] = resolve_view;
         }

         if (cmd->dynamic_pass.subpass_count > 1) {
            a = cmd->dynamic_subpasses[1].depth_stencil_attachment.attachment;
            if (a != VK_ATTACHMENT_UNUSED) {
               VK_FROM_HANDLE(tu_image_view, resolve_view,
                              common_info->resolveImageView);
               cmd->state.attachments[a] = resolve_view;
            }
         }
      }
   }

   a = cmd->dynamic_pass.fragment_density_map.attachment;
   if (a != VK_ATTACHMENT_UNUSED) {
      const VkRenderingFragmentDensityMapAttachmentInfoEXT *fdm_info =
         vk_find_struct_const(pRenderingInfo->pNext,
                              RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT_INFO_EXT);
      VK_FROM_HANDLE(tu_image_view, view, fdm_info->imageView);
      cmd->state.attachments[a] = view;
   }

   const VkRenderingAttachmentLocationInfoKHR ral_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO_KHR,
      .colorAttachmentCount = pRenderingInfo->colorAttachmentCount,
   };
   vk_cmd_set_rendering_attachment_locations(&cmd->vk, &ral_info);

   cmd->patchpoints_ctx = ralloc_context(NULL);

   a = cmd->dynamic_subpasses[0].fsr_attachment;
   if (a != VK_ATTACHMENT_UNUSED) {
      const VkRenderingFragmentShadingRateAttachmentInfoKHR *fsr_info =
         vk_find_struct_const(pRenderingInfo->pNext,
                              RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);
      VK_FROM_HANDLE(tu_image_view, view, fsr_info->imageView);
      cmd->state.attachments[a] = view;
   }

   VkResult result = tu_setup_dynamic_msrtss(cmd);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_choose_gmem_layout(cmd);

   cmd->state.renderpass_cache.pending_flush_bits =
      cmd->state.cache.pending_flush_bits;
   cmd->state.renderpass_cache.flush_bits = 0;

   bool resuming = pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT;
   bool suspending = pRenderingInfo->flags & VK_RENDERING_SUSPENDING_BIT;
   cmd->state.suspending = suspending;
   cmd->state.resuming = resuming;

   if (!resuming && cmd->device->dbg_renderpass_stomp_cs) {
      tu_cs_emit_call(&cmd->cs, cmd->device->dbg_renderpass_stomp_cs);
   }

   /* We can't track LRZ across command buffer boundaries, so we have to
    * disable LRZ when resuming/suspending unless we can track on the GPU.
    */
   if ((resuming || suspending) &&
       !cmd->device->physical_device->info->props.has_lrz_dir_tracking) {
      cmd->state.lrz.valid = false;
   } else {
      if (resuming)
         tu_lrz_begin_resumed_renderpass<CHIP>(cmd);
      else
         tu_lrz_begin_renderpass<CHIP>(cmd);
   }


   if (suspending) {
      cmd->state.suspended_pass.pass = cmd->state.pass;
      cmd->state.suspended_pass.subpass = cmd->state.subpass;
      cmd->state.suspended_pass.framebuffer = cmd->state.framebuffer;
      cmd->state.suspended_pass.render_area = cmd->state.render_area;
      cmd->state.suspended_pass.attachments = cmd->state.attachments;
      cmd->state.suspended_pass.clear_values = cmd->state.clear_values;
      cmd->state.suspended_pass.gmem_layout = cmd->state.gmem_layout;
   }

   if (!resuming) {
      tu_emit_renderpass_begin(cmd);
      tu_emit_subpass_begin<CHIP>(cmd);
   }

   if (suspending && !resuming) {
      /* entering a chain */
      switch (cmd->state.suspend_resume) {
      case SR_NONE:
         cmd->state.suspend_resume = SR_IN_CHAIN;
         break;
      case SR_AFTER_PRE_CHAIN:
         cmd->state.suspend_resume = SR_IN_CHAIN_AFTER_PRE_CHAIN;
         break;
      case SR_IN_PRE_CHAIN:
      case SR_IN_CHAIN:
      case SR_IN_CHAIN_AFTER_PRE_CHAIN:
         UNREACHABLE("suspending render pass not followed by resuming pass");
         break;
      }
   }

   if (resuming && cmd->state.suspend_resume == SR_NONE)
      cmd->state.suspend_resume = SR_IN_PRE_CHAIN;
}
TU_GENX(tu_CmdBeginRendering);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdSetRenderingAttachmentLocationsKHR(
   VkCommandBuffer commandBuffer,
   const VkRenderingAttachmentLocationInfoKHR *pLocationInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   vk_common_CmdSetRenderingAttachmentLocationsKHR(commandBuffer, pLocationInfo);

   tu6_emit_mrt<CHIP>(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_render_cntl<CHIP>(cmd, cmd->state.subpass, &cmd->draw_cs, false);

   /* Same case as a drawcall not writing to some color attachments, but not
    * trying to make LRZ work in cases where we can prove that LRZ can work.
    */
   if (cmd->state.lrz.valid)
      tu_lrz_disable_write_for_rp(cmd, "CmdSetRenderingAttachmentLocations");

   /* Because this is just a remapping and not a different "reference", there
    * doesn't need to be a barrier between accesses to the same attachment
    * with a different index. This is different from "classic" renderpasses.
    * Before a7xx the CCU includes the render target ID in the cache location
    * calculation, so we need to manually flush/invalidate color CCU here
    * since the same render target/attachment may be in a different location.
    */
   if (cmd->device->physical_device->info->chip == 6) {
      struct tu_cache_state *cache = &cmd->state.renderpass_cache;
      tu_flush_for_access(cache, TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE,
                          TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE);
      cache->flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
   }
}
TU_GENX(tu_CmdSetRenderingAttachmentLocationsKHR);

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetRenderingInputAttachmentIndicesKHR(
   VkCommandBuffer commandBuffer,
   const VkRenderingInputAttachmentIndexInfoKHR *pLocationInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   vk_common_CmdSetRenderingInputAttachmentIndicesKHR(commandBuffer, pLocationInfo);

   const struct vk_input_attachment_location_state *ial =
      &cmd->vk.dynamic_graphics_state.ial;

   struct tu_subpass *subpass = &cmd->dynamic_subpasses[0];

   for (unsigned i = 0; i < ARRAY_SIZE(cmd->dynamic_input_attachments); i++) {
      subpass->input_attachments[i].attachment = VK_ATTACHMENT_UNUSED;
   }

   unsigned input_count = 0;
   for (unsigned i = 0; i < subpass->color_count; i++) {
      if (ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
         continue;
      subpass->input_attachments[ial->color_map[i] + TU_DYN_INPUT_ATT_OFFSET].attachment =
         subpass->color_attachments[i].attachment;
      input_count = MAX2(input_count, ial->color_map[i] + TU_DYN_INPUT_ATT_OFFSET + 1);
   }

   if (ial->depth_att != MESA_VK_ATTACHMENT_UNUSED) {
      if (ial->depth_att == MESA_VK_ATTACHMENT_NO_INDEX) {
         subpass->input_attachments[0].attachment =
            subpass->depth_stencil_attachment.attachment;
         input_count = MAX2(input_count, 1);
      } else {
         subpass->input_attachments[ial->depth_att + TU_DYN_INPUT_ATT_OFFSET].attachment =
            subpass->depth_stencil_attachment.attachment;
         input_count = MAX2(input_count, ial->depth_att + TU_DYN_INPUT_ATT_OFFSET + 1);
      }
   }

   if (ial->stencil_att != MESA_VK_ATTACHMENT_UNUSED) {
      if (ial->stencil_att == MESA_VK_ATTACHMENT_NO_INDEX) {
         subpass->input_attachments[0].attachment =
            subpass->depth_stencil_attachment.attachment;
         input_count = MAX2(input_count, 1);
      } else {
         subpass->input_attachments[ial->stencil_att + TU_DYN_INPUT_ATT_OFFSET].attachment =
            subpass->depth_stencil_attachment.attachment;
         input_count = MAX2(input_count, ial->stencil_att + TU_DYN_INPUT_ATT_OFFSET + 1);
      }
   }

   subpass->input_count = input_count;

   tu_set_input_attachments(cmd, subpass);
}

static void
tu_next_subpass_lrz(struct tu_cmd_buffer *cmd,
                    const struct tu_subpass *subpass,
                    const struct tu_subpass *new_subpass)
{
   /* Track LRZ valid state
    *
    * TODO: Improve this tracking for keeping the state of the past depth/stencil images,
    * so if they become active again, we reuse its old state.
    */
   if (new_subpass->depth_stencil_attachment.attachment != subpass->depth_stencil_attachment.attachment) {
      cmd->state.lrz.valid = false;
      cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
   }
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                   const VkSubpassBeginInfo *pSubpassBeginInfo,
                   const VkSubpassEndInfo *pSubpassEndInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (TU_DEBUG(DYNAMIC)) {
      vk_common_CmdNextSubpass2(commandBuffer, pSubpassBeginInfo,
                                pSubpassEndInfo);
      return;
   }

   struct tu_cs *cs = &cmd->draw_cs;

   const struct tu_subpass *subpass = cmd->state.subpass++;
   const struct tu_subpass *new_subpass = cmd->state.subpass;

   tu_next_subpass_lrz(cmd, subpass, new_subpass);

   if (cmd->state.tiling->possible) {
      if (cmd->state.pass->has_fdm)
         tu_cs_set_writeable(cs, true);

      tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

      struct tu_resolve_group resolve_group = {};

      tu6_emit_gmem_stores<CHIP>(cmd, &cmd->draw_cs, &resolve_group, subpass);

      tu_emit_resolve_group<CHIP>(cmd, cs, &resolve_group);

      tu_cond_exec_end(cs);

      if (cmd->state.pass->has_fdm)
         tu_cs_set_writeable(cs, false);

      tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);
   }

   tu6_emit_sysmem_resolves<CHIP>(cmd, cs, subpass);

   if (cmd->state.tiling->possible)
      tu_cond_exec_end(cs);

   /* Handle dependencies for the next subpass */
   tu_subpass_barrier(cmd, &cmd->state.subpass->start_barrier, false);

   if (cmd->state.subpass->feedback_invalidate) {
      cmd->state.renderpass_cache.flush_bits |=
         TU_CMD_FLAG_CACHE_INVALIDATE | TU_CMD_FLAG_BLIT_CACHE_CLEAN |
         TU_CMD_FLAG_WAIT_FOR_IDLE;
   }

   tu_emit_subpass_begin<CHIP>(cmd);
}
TU_GENX(tu_CmdNextSubpass2);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginCustomResolveEXT(VkCommandBuffer commandBuffer,
                            const VkBeginCustomResolveInfoEXT *pBeginShaderResolveInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   const struct tu_subpass *subpass = &cmd->dynamic_subpasses[0];

   const struct tu_subpass *new_subpass = &cmd->dynamic_subpasses[1];
   cmd->state.subpass = new_subpass;

   tu_next_subpass_lrz(cmd, subpass, new_subpass);

   tu_emit_subpass_begin<CHIP>(cmd);
}
TU_GENX(tu_CmdBeginCustomResolveEXT);

static uint32_t
tu6_user_consts_size(const struct tu_const_state *const_state,
                     bool ldgk,
                     mesa_shader_stage type)
{
   uint32_t dwords = 0;

   if (const_state->push_consts.type == IR3_PUSH_CONSTS_PER_STAGE) {
      unsigned num_units = const_state->push_consts.dwords;
      dwords += 4 + num_units;
      assert(num_units > 0);
   }

   if (ldgk) {
      dwords += 6 + (2 * const_state->num_inline_ubos + 4);
   } else {
      dwords += 8 * const_state->num_inline_ubos;
   }

   return dwords;
}

static void
tu6_emit_per_stage_push_consts(struct tu_cs *cs,
                               const struct tu_const_state *const_state,
                               const struct ir3_const_state *ir_const_state,
                               mesa_shader_stage type,
                               uint32_t *push_constants)
{
   if (const_state->push_consts.type == IR3_PUSH_CONSTS_PER_STAGE) {
      unsigned num_units = const_state->push_consts.dwords;
      unsigned offset_vec4 =
         ir_const_state->allocs.consts[IR3_CONST_ALLOC_PUSH_CONSTS]
            .offset_vec4;
      assert(num_units > 0);

      /* DST_OFF and NUM_UNIT requires vec4 units */
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3 + num_units);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset_vec4) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
            CP_LOAD_STATE6_0_NUM_UNIT(num_units / 4));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);

      unsigned lo = const_state->push_consts.lo_dwords;
      for (unsigned i = 0; i < num_units; i++)
         tu_cs_emit(cs, push_constants[i + lo]);
   }
}

static void
tu6_emit_inline_ubo(struct tu_cs *cs,
                    const struct tu_const_state *const_state,
                    unsigned constlen,
                    mesa_shader_stage type,
                    struct tu_descriptor_state *descriptors)
{
   assert(const_state->num_inline_ubos == 0 || !cs->device->physical_device->info->props.load_shader_consts_via_preamble);

   /* Emit loads of inline uniforms. These load directly from the uniform's
    * storage space inside the descriptor set.
    */
   for (unsigned i = 0; i < const_state->num_inline_ubos; i++) {
      const struct tu_inline_ubo *ubo = &const_state->ubos[i];

      if (constlen <= ubo->const_offset_vec4)
         continue;

      uint64_t va = descriptors->set_iova[ubo->base] & ~0x3f;

      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), ubo->push_address ? 7 : 3);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(ubo->const_offset_vec4) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(ubo->push_address ? SS6_DIRECT : SS6_INDIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
            CP_LOAD_STATE6_0_NUM_UNIT(MIN2(ubo->size_vec4, constlen - ubo->const_offset_vec4)));
      if (ubo->push_address) {
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
         tu_cs_emit_qw(cs, va + ubo->offset);
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
      } else {
         tu_cs_emit_qw(cs, va + ubo->offset);
      }
   }
}

static void
tu7_emit_inline_ubo(struct tu_cs *cs,
                    const struct tu_const_state *const_state,
                    const struct ir3_const_state *ir_const_state,
                    unsigned constlen,
                    mesa_shader_stage type,
                    struct tu_descriptor_state *descriptors)
{
   uint64_t addresses[7] = {0};
   unsigned offset = const_state->inline_uniforms_ubo.idx;

   if (offset == -1)
      return;

   for (unsigned i = 0; i < const_state->num_inline_ubos; i++) {
      const struct tu_inline_ubo *ubo = &const_state->ubos[i];

      uint64_t va = descriptors->set_iova[ubo->base] & ~0x3f;
      addresses[i] = va + ubo->offset;
   }

   /* A7XX TODO: Emit data via sub_cs instead of NOP */
   uint64_t iova = tu_cs_emit_data_nop(cs, (uint32_t *)addresses, const_state->num_inline_ubos * 2, 4);

   tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 5);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_UBO) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
            CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   int size_vec4s = DIV_ROUND_UP(const_state->num_inline_ubos * 2, 4);
   tu_cs_emit_qw(cs, iova | ((uint64_t)A6XX_UBO_1_SIZE(size_vec4s) << 32));
}

static void
tu_emit_inline_ubo(struct tu_cs *cs,
                   const struct tu_const_state *const_state,
                   const struct ir3_const_state *ir_const_state,
                   unsigned constlen,
                   mesa_shader_stage type,
                   struct tu_descriptor_state *descriptors)
{
   if (!const_state->num_inline_ubos)
      return;

   if (cs->device->physical_device->info->props.load_inline_uniforms_via_preamble_ldgk) {
      tu7_emit_inline_ubo(cs, const_state, ir_const_state, constlen, type, descriptors);
   } else {
      tu6_emit_inline_ubo(cs, const_state, constlen, type, descriptors);
   }
}

static void
tu6_emit_shared_consts(struct tu_cs *cs,
                       const struct tu_push_constant_range *shared_consts,
                       uint32_t *push_constants,
                       bool compute)
{
   if (shared_consts->dwords > 0) {
      /* Offset and num_units for shared consts are in units of dwords. */
      unsigned num_units = shared_consts->dwords;
      unsigned offset = shared_consts->lo_dwords;

      enum a6xx_state_type st = compute ? ST6_UBO : ST6_CONSTANTS;
      uint32_t cp_load_state = compute ? CP_LOAD_STATE6_FRAG : CP_LOAD_STATE6;

      tu_cs_emit_pkt7(cs, cp_load_state, 3 + num_units);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
            CP_LOAD_STATE6_0_STATE_TYPE(st) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(SB6_UAV) |
            CP_LOAD_STATE6_0_NUM_UNIT(num_units));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);

      for (unsigned i = 0; i < num_units; i++)
         tu_cs_emit(cs, push_constants[i + offset]);
   }
}

static void
tu7_emit_shared_preamble_consts(
   struct tu_cs *cs,
   const struct tu_push_constant_range *shared_consts,
   uint32_t *push_constants)
{
   tu_cs_emit_pkt4(cs, REG_A7XX_SP_SHARED_CONSTANT_GFX(shared_consts->lo_dwords),
                   shared_consts->dwords);
   tu_cs_emit_array(cs, push_constants + shared_consts->lo_dwords,
                    shared_consts->dwords);
}

static uint32_t
tu6_const_size(struct tu_cmd_buffer *cmd,
               const struct tu_push_constant_range *shared_consts,
               bool compute)
{
   uint32_t dwords = 0;

   if (shared_consts->type == IR3_PUSH_CONSTS_SHARED) {
      dwords += shared_consts->dwords + 4;
   } else if (shared_consts->type == IR3_PUSH_CONSTS_SHARED_PREAMBLE) {
      dwords += shared_consts->dwords + 1;
   }

   bool ldgk = cmd->device->physical_device->info->props.load_inline_uniforms_via_preamble_ldgk;
   if (compute) {
      dwords +=
         tu6_user_consts_size(&cmd->state.shaders[MESA_SHADER_COMPUTE]->const_state, ldgk, MESA_SHADER_COMPUTE);
   } else {
      for (uint32_t type = MESA_SHADER_VERTEX; type <= MESA_SHADER_FRAGMENT; type++)
         dwords += tu6_user_consts_size(&cmd->state.shaders[type]->const_state, ldgk, (mesa_shader_stage) type);
   }

   return dwords;
}

static struct tu_draw_state
tu_emit_consts(struct tu_cmd_buffer *cmd, bool compute)
{
   uint32_t dwords = 0;
   const struct tu_push_constant_range *shared_consts =
      compute ? &cmd->state.shaders[MESA_SHADER_COMPUTE]->const_state.push_consts :
      &cmd->state.program.shared_consts;

   dwords = tu6_const_size(cmd, shared_consts, compute);

   if (dwords == 0)
      return (struct tu_draw_state) {};

   struct tu_cs cs;
   tu_cs_begin_sub_stream(&cmd->sub_cs, dwords, &cs);

   if (shared_consts->type == IR3_PUSH_CONSTS_SHARED) {
      tu6_emit_shared_consts(&cs, shared_consts, cmd->push_constants, compute);
   } else if (shared_consts->type == IR3_PUSH_CONSTS_SHARED_PREAMBLE) {
      tu7_emit_shared_preamble_consts(&cs, shared_consts, cmd->push_constants);
   }

   if (compute) {
      tu6_emit_per_stage_push_consts(
         &cs, &cmd->state.shaders[MESA_SHADER_COMPUTE]->const_state,
         cmd->state.shaders[MESA_SHADER_COMPUTE]->variant->const_state,
         MESA_SHADER_COMPUTE, cmd->push_constants);
      tu_emit_inline_ubo(
         &cs, &cmd->state.shaders[MESA_SHADER_COMPUTE]->const_state,
         cmd->state.shaders[MESA_SHADER_COMPUTE]->variant->const_state,
         cmd->state.shaders[MESA_SHADER_COMPUTE]->variant->constlen,
         MESA_SHADER_COMPUTE,
         tu_get_descriptors_state(cmd, VK_PIPELINE_BIND_POINT_COMPUTE));
   } else {
      struct tu_descriptor_state *descriptors =
         tu_get_descriptors_state(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
      for (uint32_t type = MESA_SHADER_VERTEX; type <= MESA_SHADER_FRAGMENT; type++) {
         const struct tu_program_descriptor_linkage *link =
            &cmd->state.program.link[type];
         tu6_emit_per_stage_push_consts(&cs, &link->tu_const_state,
                                        &link->const_state,
                                        (mesa_shader_stage) type,
                                        cmd->push_constants);
         tu_emit_inline_ubo(&cs, &link->tu_const_state,
                            &link->const_state, link->constlen,
                            (mesa_shader_stage) type, descriptors);
      }
   }

   return tu_cs_end_draw_state(&cmd->sub_cs, &cs);
}

/* Returns true if stencil may be written when depth test fails.
 * This could be either from stencil written on depth test fail itself,
 * or stencil written on the stencil test failure where subsequent depth
 * test may also fail.
 */
static bool
tu6_stencil_written_on_depth_fail(
   const struct vk_stencil_test_face_state *face)
{
   switch (face->op.compare) {
   case VK_COMPARE_OP_ALWAYS:
      /* The stencil op always passes, no need to worry about failOp. */
      return face->op.depth_fail != VK_STENCIL_OP_KEEP;
   case VK_COMPARE_OP_NEVER:
      /* The stencil op always fails, so failOp will always be used. */
      return face->op.fail != VK_STENCIL_OP_KEEP;
   default:
      /* If the stencil test fails, depth may fail as well, so we can write
       * stencil when the depth fails if failOp is not VK_STENCIL_OP_KEEP.
       */
      return face->op.fail != VK_STENCIL_OP_KEEP ||
             face->op.depth_fail != VK_STENCIL_OP_KEEP;
   }
}

/* Returns true if the stencil write result may change based on the result of a
 * depth test.
 */
static bool
tu6_stencil_written_based_on_depth_test(
   const struct vk_stencil_test_face_state *face)
{
   switch (face->op.compare) {
   case VK_COMPARE_OP_ALWAYS:
      /* The stencil op always passes, no need to worry about failOp. */
      return face->op.depth_fail != VK_STENCIL_OP_KEEP ||
             face->op.pass != VK_STENCIL_OP_KEEP;
   case VK_COMPARE_OP_NEVER:
      /* The stencil op always fails, so failOp will always be used. */
      return face->op.fail != VK_STENCIL_OP_KEEP;
   default:
      /* If the stencil test fails, depth may fail as well, so we can write
       * stencil when the depth fails if failOp is not VK_STENCIL_OP_KEEP.
       */
      return face->op.fail != VK_STENCIL_OP_KEEP ||
             face->op.pass != VK_STENCIL_OP_KEEP ||
             face->op.depth_fail != VK_STENCIL_OP_KEEP;
   }
}

/* Various frontends (ANGLE, zink at least) will enable stencil testing with
 * what works out to be no-op writes.  Simplify what they give us into flags
 * that LRZ can use.
 */
static void
tu6_update_simplified_stencil_state(struct tu_cmd_buffer *cmd)
{
   const struct vk_depth_stencil_state *ds =
      &cmd->vk.dynamic_graphics_state.ds;
   bool stencil_test_enable = ds->stencil.test_enable;

   if (!stencil_test_enable) {
      cmd->state.stencil_front_write = false;
      cmd->state.stencil_back_write = false;
      cmd->state.stencil_written_on_depth_fail = false;
      cmd->state.stencil_written_based_on_depth_test = false;
      return;
   }

   bool stencil_front_writemask = ds->stencil.front.write_mask;
   bool stencil_back_writemask = ds->stencil.back.write_mask;

   VkStencilOp front_fail_op = (VkStencilOp)ds->stencil.front.op.fail;
   VkStencilOp front_pass_op = (VkStencilOp)ds->stencil.front.op.pass;
   VkStencilOp front_depth_fail_op = (VkStencilOp)ds->stencil.front.op.depth_fail;
   VkStencilOp back_fail_op = (VkStencilOp)ds->stencil.back.op.fail;
   VkStencilOp back_pass_op = (VkStencilOp)ds->stencil.back.op.pass;
   VkStencilOp back_depth_fail_op = (VkStencilOp)ds->stencil.back.op.depth_fail;

   bool stencil_front_op_writes =
      front_pass_op != VK_STENCIL_OP_KEEP ||
      front_fail_op != VK_STENCIL_OP_KEEP ||
      front_depth_fail_op != VK_STENCIL_OP_KEEP;

   bool stencil_back_op_writes =
      back_pass_op != VK_STENCIL_OP_KEEP ||
      back_fail_op != VK_STENCIL_OP_KEEP ||
      back_depth_fail_op != VK_STENCIL_OP_KEEP;

   cmd->state.stencil_front_write =
      stencil_front_op_writes && stencil_front_writemask;
   cmd->state.stencil_back_write =
      stencil_back_op_writes && stencil_back_writemask;
   cmd->state.stencil_written_on_depth_fail =
      (cmd->state.stencil_front_write &&
       tu6_stencil_written_on_depth_fail(&ds->stencil.front)) ||
      (cmd->state.stencil_back_write &&
       tu6_stencil_written_on_depth_fail(&ds->stencil.back));
   cmd->state.stencil_written_based_on_depth_test =
      (cmd->state.stencil_front_write &&
       tu6_stencil_written_based_on_depth_test(&ds->stencil.front)) ||
      (cmd->state.stencil_back_write &&
       tu6_stencil_written_based_on_depth_test(&ds->stencil.back));
}

static bool
tu6_writes_depth(struct tu_cmd_buffer *cmd, bool depth_test_enable)
{
   bool depth_write_enable =
      cmd->vk.dynamic_graphics_state.ds.depth.write_enable;

   VkCompareOp depth_compare_op = (VkCompareOp)
      cmd->vk.dynamic_graphics_state.ds.depth.compare_op;

   bool depth_compare_op_writes = depth_compare_op != VK_COMPARE_OP_NEVER;

   return depth_test_enable && depth_write_enable && depth_compare_op_writes;
}

static bool
tu6_writes_stencil(struct tu_cmd_buffer *cmd)
{
   return cmd->state.stencil_front_write || cmd->state.stencil_back_write;
}

static bool
tu_fs_reads_dynamic_ds_input_attachment(struct tu_cmd_buffer *cmd,
                                        const struct tu_shader *fs)
{
   uint8_t depth_att = cmd->vk.dynamic_graphics_state.ial.depth_att;
   if (depth_att == MESA_VK_ATTACHMENT_UNUSED)
      return false;
   unsigned depth_idx =
      (depth_att == MESA_VK_ATTACHMENT_NO_INDEX) ? 0 : depth_att + 1;
   return fs->fs.dynamic_input_attachments_used & (1u << depth_idx);
}

template <chip CHIP>
static void
tu6_build_depth_plane_z_mode(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   enum a6xx_ztest_mode zmode = A6XX_EARLY_Z;
   bool depth_test_enable = cmd->vk.dynamic_graphics_state.ds.depth.test_enable;
   bool stencil_test_enable = cmd->vk.dynamic_graphics_state.ds.stencil.test_enable;
   bool ds_test_enable = depth_test_enable || stencil_test_enable;
   bool depth_write = tu6_writes_depth(cmd, depth_test_enable);
   bool stencil_write = tu6_writes_stencil(cmd);
   const struct tu_shader *fs = cmd->state.shaders[MESA_SHADER_FRAGMENT];
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_subpass *subpass = cmd->state.subpass;

   VkFormat depth_format = VK_FORMAT_UNDEFINED;
   if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED)
      depth_format = pass->attachments[subpass->depth_stencil_attachment.attachment].format;

   bool fs_kill_fragments =
      fs->variant->has_kill ||
      /* EARLY_Z causes D/S to be written before FS but gl_SampleMask can
       * kill fragments, we cannot have EARLY_Z + gl_SampleMask + D/S writes.
       */
      fs->variant->writes_smask ||
      /* Alpha-to-coverage behaves like a discard. */
      cmd->vk.dynamic_graphics_state.ms.alpha_to_coverage_enable;

   if ((fs_kill_fragments ||
        (cmd->state.pipeline_feedback_loops & VK_IMAGE_ASPECT_DEPTH_BIT) ||
        (cmd->vk.dynamic_graphics_state.feedback_loops &
         VK_IMAGE_ASPECT_DEPTH_BIT) ||
        tu_fs_reads_dynamic_ds_input_attachment(cmd, fs)) &&
       (depth_write || stencil_write)) {
      zmode = A6XX_EARLY_Z_LATE_Z;
   }

   /* If there is explicit depth direction in FS writing gl_FragDepth
    * may be compatible with LRZ test.
    */
   if (cmd->state.lrz.enabled && fs->variant->writes_pos &&
       zmode == A6XX_EARLY_Z) {
      zmode = A6XX_EARLY_Z_LATE_Z;
   }

   /* "EARLY_Z + discard" would yield incorrect occlusion query result,
    * since Vulkan expects occlusion query to happen after fragment shader.
    */
   if (zmode == A6XX_EARLY_Z && fs_kill_fragments &&
       cmd->state.occlusion_query_may_be_running)
      zmode = A6XX_EARLY_Z_LATE_Z;

   VkCompareOp compare_op =
      cmd->vk.dynamic_graphics_state.ds.depth.compare_op;
   /* This state combination wedges something in GPU causing hang.
    * Forcing A6XX_LATE_Z prevents it. Prop driver does the same.
    */
   if (zmode == A6XX_EARLY_Z_LATE_Z && !depth_write &&
       cmd->state.occlusion_query_may_be_running &&
       (compare_op == VK_COMPARE_OP_ALWAYS ||
        compare_op == VK_COMPARE_OP_NEVER)) {
      zmode = A6XX_LATE_Z;
   }

   if (zmode == A6XX_EARLY_Z_LATE_Z &&
       (cmd->state.stencil_written_on_depth_fail || fs->fs.sample_shading ||
        !vk_format_has_depth(depth_format) || !ds_test_enable)) {
      zmode = A6XX_LATE_Z;
   }

   if ((stencil_test_enable && depth_format == VK_FORMAT_S8_UINT) ||
       (ds_test_enable &&
        (fs->fs.lrz.force_late_z || cmd->state.lrz.force_late_z)))
      zmode = A6XX_LATE_Z;

   /* User defined early tests take precedence above all else */
   if (fs->variant->fs.early_fragment_tests)
      zmode = A6XX_EARLY_Z;

   /* FS bypass requires early Z */
   if (cmd->state.disable_fs)
      zmode = A6XX_EARLY_Z;

   tu_cs_emit_regs(cs, GRAS_SU_DEPTH_PLANE_CNTL(CHIP, .z_mode = zmode));
   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_PLANE_CNTL(.z_mode = zmode));
}

static uint32_t
fs_params_offset(struct tu_cmd_buffer *cmd)
{
   const struct tu_program_descriptor_linkage *link =
      &cmd->state.program.link[MESA_SHADER_FRAGMENT];
   const struct ir3_const_state *const_state = &link->const_state;

   if (const_state->num_driver_params <= IR3_DP_FS_DYNAMIC)
      return 0;

   uint32_t param_offset =
      const_state->allocs.consts[IR3_CONST_ALLOC_DRIVER_PARAMS].offset_vec4;

   if (param_offset + IR3_DP_FS_DYNAMIC / 4 >= link->constlen)
      return 0;

   return param_offset + IR3_DP_FS_DYNAMIC / 4;
}

static uint32_t
fs_params_size(struct tu_cmd_buffer *cmd)
{
   const struct tu_program_descriptor_linkage *link =
      &cmd->state.program.link[MESA_SHADER_FRAGMENT];
   const struct ir3_const_state *const_state = &link->const_state;

   return DIV_ROUND_UP(const_state->num_driver_params - IR3_DP_FS_DYNAMIC, 4);
}

struct apply_fs_params_state {
   unsigned num_consts;
   bool custom_resolve;
};

static void
fdm_apply_fs_params(struct tu_cmd_buffer *cmd,
                    struct tu_cs *cs,
                    void *data,
                    VkOffset2D common_bin_offset,
                    const VkOffset2D *hw_viewport_offsets,
                    unsigned views,
                    const VkExtent2D *frag_areas,
                    const VkRect2D *bins,
                    bool binning)
{
   const struct apply_fs_params_state *state =
      (const struct apply_fs_params_state *)data;
   unsigned num_consts = state->num_consts;

   for (unsigned i = 0; i < DIV_ROUND_UP(num_consts, 2); i++) {
      /* FDM per layer may be enabled in the shader but not in the renderpass,
       * in which case views will be 1 and we have to replicate the one view
       * to all of the layers.
       */
      VkExtent2D area = frag_areas[MIN2(i, views - 1)];
      VkRect2D bin = bins[MIN2(i, views - 1)];
      VkOffset2D offset = tu_fdm_per_bin_offset(area, bin, common_bin_offset);

      /* For custom resolve, we switch to rendering directly to sysmem and so
       * the fragment size becomes 1x1. This means we have to scale down
       * FragCoord when accessing GMEM input attachments.
       *
       * TODO: When we support subsampled images, this should also only happen
       * for non-subsampled images.
       */
      if (state->custom_resolve) {
         tu_cs_emit(cs, 1 /* width */);
         tu_cs_emit(cs, 1 /* height */);
         tu_cs_emit(cs, fui(0.0));
         tu_cs_emit(cs, fui(0.0));
      } else {
         tu_cs_emit(cs, area.width);
         tu_cs_emit(cs, area.height);
         tu_cs_emit(cs, fui(offset.x));
         tu_cs_emit(cs, fui(offset.y));
      }

      if (i * 2 + 1 < num_consts) {
         if (state->custom_resolve) {
            tu_cs_emit(cs, fui(1. / area.width));
            tu_cs_emit(cs, fui(1. / area.height));
            tu_cs_emit(cs, fui(offset.x));
            tu_cs_emit(cs, fui(offset.y));
         } else {
            tu_cs_emit(cs, fui(1.0));
            tu_cs_emit(cs, fui(1.0));
            tu_cs_emit(cs, fui(0.0));
            tu_cs_emit(cs, fui(0.0));
         }
      }
   }
}

static void
tu_emit_fdm_params(struct tu_cmd_buffer *cmd,
                   struct tu_cs *cs, struct tu_shader *fs,
                   unsigned num_units)
{
   STATIC_ASSERT(IR3_DP_FS(frag_invocation_count) == IR3_DP_FS_DYNAMIC);
   tu_cs_emit(cs, fs->fs.sample_shading ?
              cmd->vk.dynamic_graphics_state.ms.rasterization_samples : 1);
   tu_cs_emit(cs, 0);
   tu_cs_emit(cs, 0);
   tu_cs_emit(cs, 0);

   STATIC_ASSERT(IR3_DP_FS(frag_size) == IR3_DP_FS_DYNAMIC + 4);
   STATIC_ASSERT(IR3_DP_FS(frag_offset) == IR3_DP_FS_DYNAMIC + 6);
   if (num_units > 1) {
      if (fs->fs.has_fdm) {
         struct apply_fs_params_state state = {
            .num_consts = num_units - 1,
            .custom_resolve = cmd->state.subpass->custom_resolve,
         };
         tu_create_fdm_bin_patchpoint(cmd, cs, 4 * (num_units - 1),
                                      TU_FDM_SKIP_BINNING,
                                      fdm_apply_fs_params, state);
      } else {
         for (unsigned i = 0; i < DIV_ROUND_UP((num_units - 1), 2); i++) {
            tu_cs_emit(cs, 1);
            tu_cs_emit(cs, 1);
            tu_cs_emit(cs, fui(0.0f));
            tu_cs_emit(cs, fui(0.0f));
            if (i * 2 + 1 < num_units - 1) {
               tu_cs_emit(cs, fui(1.0));
               tu_cs_emit(cs, fui(1.0));
               tu_cs_emit(cs, fui(0.0));
               tu_cs_emit(cs, fui(0.0));
            }
         }
      }
   }
}

static void
tu6_emit_fs_params(struct tu_cmd_buffer *cmd)
{
   uint32_t offset = fs_params_offset(cmd);

   if (offset == 0) {
      cmd->state.fs_params = (struct tu_draw_state) {};
      return;
   }

   struct tu_shader *fs = cmd->state.shaders[MESA_SHADER_FRAGMENT];

   unsigned num_units = fs_params_size(cmd);

   if (fs->fs.has_fdm)
      tu_cs_set_writeable(&cmd->sub_cs, true);

   struct tu_cs cs;
   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 4 + 4 * num_units, &cs);
   if (result != VK_SUCCESS) {
      tu_cs_set_writeable(&cmd->sub_cs, false);
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_FRAG, 3 + 4 * num_units);
   tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
         CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
         CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
         CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_SHADER) |
         CP_LOAD_STATE6_0_NUM_UNIT(num_units));
   tu_cs_emit(&cs, 0);
   tu_cs_emit(&cs, 0);

   tu_emit_fdm_params(cmd, &cs, fs, num_units);

   cmd->state.fs_params = tu_cs_end_draw_state(&cmd->sub_cs, &cs);

   if (fs->fs.has_fdm)
      tu_cs_set_writeable(&cmd->sub_cs, false);
}

static void
tu7_emit_fs_params(struct tu_cmd_buffer *cmd)
{
   struct tu_shader *fs = cmd->state.shaders[MESA_SHADER_FRAGMENT];

   int ubo_offset = fs->const_state.fdm_ubo.idx;
   if (ubo_offset < 0) {
      cmd->state.fs_params = (struct tu_draw_state) {};
      return;
   }

   unsigned num_units = DIV_ROUND_UP(fs->const_state.fdm_ubo.size, 4);

   if (fs->fs.has_fdm)
      tu_cs_set_writeable(&cmd->sub_cs, true);

   struct tu_cs cs;
   VkResult result =
      tu_cs_begin_sub_stream_aligned(&cmd->sub_cs, num_units, 4, &cs);
   if (result != VK_SUCCESS) {
      tu_cs_set_writeable(&cmd->sub_cs, false);
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_emit_fdm_params(cmd, &cs, fs, num_units);

   struct tu_draw_state fdm_ubo = tu_cs_end_draw_state(&cmd->sub_cs, &cs);

   if (fs->fs.has_fdm)
      tu_cs_set_writeable(&cmd->sub_cs, false);

   result = tu_cs_begin_sub_stream(&cmd->sub_cs, 6, &cs);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_FRAG, 5);
   tu_cs_emit(&cs,
              CP_LOAD_STATE6_0_DST_OFF(ubo_offset) |
              CP_LOAD_STATE6_0_STATE_TYPE(ST6_UBO)|
              CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
              CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_SHADER) |
              CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit(&cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(&cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   tu_cs_emit_qw(&cs,
                 fdm_ubo.iova |
                 (uint64_t)A6XX_UBO_1_SIZE(num_units) << 32);

   cmd->state.fs_params = tu_cs_end_draw_state(&cmd->sub_cs, &cs);
}

static void
tu_emit_fs_params(struct tu_cmd_buffer *cmd)
{
   if (cmd->device->compiler->load_shader_consts_via_preamble)
      tu7_emit_fs_params(cmd);
   else
      tu6_emit_fs_params(cmd);
}

static void
tu_flush_dynamic_input_attachments(struct tu_cmd_buffer *cmd)
{
   struct tu_shader *fs = cmd->state.shaders[MESA_SHADER_FRAGMENT];

   if (!fs->fs.dynamic_input_attachments_used)
      return;

   /* Input attachments may read data from a load op, so we have to invalidate
    * UCHE and force pending blits to complete unless we know it's already
    * been invalidated. This is the same as tu_subpass::feedback_invalidate
    * but for dynamic renderpasses.
    */
   if (!cmd->state.blit_cache_cleaned) {
      cmd->state.renderpass_cache.flush_bits |=
         TU_CMD_FLAG_CACHE_INVALIDATE | TU_CMD_FLAG_BLIT_CACHE_CLEAN |
         TU_CMD_FLAG_WAIT_FOR_IDLE;
   }
}

template <chip CHIP>
static VkResult
tu6_draw_common(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                bool indexed,
                /* note: draw_count is 0 for indirect */
                uint32_t draw_count)
{
   const struct tu_program_state *program = &cmd->state.program;
   struct tu_render_pass_state *rp = &cmd->state.rp;

   trace_start_draw(
      &cmd->rp_trace, &cmd->draw_cs, cmd, draw_count,
      cmd->state.program.stage_sha1[MESA_SHADER_VERTEX],
      cmd->state.program.stage_sha1[MESA_SHADER_TESS_CTRL],
      cmd->state.program.stage_sha1[MESA_SHADER_TESS_EVAL],
      cmd->state.program.stage_sha1[MESA_SHADER_GEOMETRY],
      cmd->state.program.stage_sha1[MESA_SHADER_FRAGMENT]);

   /* Emit state first, because it's needed for bandwidth calculations */
   uint32_t dynamic_draw_state_dirty = 0;
   if (!BITSET_IS_EMPTY(cmd->vk.dynamic_graphics_state.dirty) ||
       (cmd->state.dirty & ~TU_CMD_DIRTY_COMPUTE_DESC_SETS)) {
      dynamic_draw_state_dirty = tu_emit_draw_state<CHIP>(cmd);
   }

   /* Primitive restart value works in non-indexed draws, we have to disable
    * prim restart for such draws since we may read stale restart index.
    */
   if (cmd->state.last_draw_indexed != indexed) {
      cmd->state.last_draw_indexed = indexed;
      BITSET_SET(cmd->vk.dynamic_graphics_state.dirty,
                 MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE);
   }

   /* Fill draw stats for autotuner */
   rp->drawcall_count++;

   rp->drawcall_bandwidth_per_sample_sum +=
      cmd->state.bandwidth.color_bandwidth_per_sample;

   /* add depth memory bandwidth cost */
   const uint32_t depth_bandwidth = cmd->state.bandwidth.depth_cpp_per_sample;
   if (cmd->vk.dynamic_graphics_state.ds.depth.write_enable)
      rp->drawcall_bandwidth_per_sample_sum += depth_bandwidth;
   if (cmd->vk.dynamic_graphics_state.ds.depth.test_enable)
      rp->drawcall_bandwidth_per_sample_sum += depth_bandwidth;

   /* add stencil memory bandwidth cost */
   const uint32_t stencil_bandwidth =
      cmd->state.bandwidth.stencil_cpp_per_sample;
   if (cmd->vk.dynamic_graphics_state.ds.stencil.test_enable)
      rp->drawcall_bandwidth_per_sample_sum += stencil_bandwidth * 2;

   if (cmd->state.dirty & TU_CMD_DIRTY_FS)
      tu_flush_dynamic_input_attachments(cmd);

   tu_emit_cache_flush_renderpass<CHIP>(cmd);

  if (BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX) ||
      (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      bool primitive_restart_enabled =
         cmd->vk.dynamic_graphics_state.ia.primitive_restart_enable;

      bool primitive_restart = primitive_restart_enabled && indexed;
      bool provoking_vtx_last =
         cmd->vk.dynamic_graphics_state.rs.provoking_vertex ==
         VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;

      uint32_t primitive_cntl_0 =
         PC_CNTL(CHIP, .primitive_restart = primitive_restart,
                 .provoking_vtx_last = provoking_vtx_last)
            .value;
      tu_cs_emit_regs(cs, PC_CNTL(CHIP, .dword = primitive_cntl_0));
      if (CHIP == A7XX) {
         tu_cs_emit_regs(cs, VPC_PC_CNTL(CHIP, .dword = primitive_cntl_0));
      }
   }

   struct tu_tess_params *tess_params = &cmd->state.tess_params;
   if ((cmd->state.dirty & TU_CMD_DIRTY_TESS_PARAMS) ||
       BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN) ||
       (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      bool tess_upper_left_domain_origin =
         (VkTessellationDomainOrigin)cmd->vk.dynamic_graphics_state.ts.domain_origin ==
         VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT;
      tu_cs_emit_regs(cs, PC_DS_PARAM(CHIP,
            .spacing = tess_params->spacing,
            .output = tess_upper_left_domain_origin ?
               tess_params->output_upper_left :
               tess_params->output_lower_left));
   }

   if (cmd->device->physical_device->info->props.has_rt_workaround &&
       cmd->state.program.uses_ray_intersection) {
      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_SHADER_USES_RT);
   }

   /* Early exit if there is nothing to emit, saves CPU cycles */
   uint32_t dirty = cmd->state.dirty;
   if (!dynamic_draw_state_dirty && !(dirty & ~TU_CMD_DIRTY_COMPUTE_DESC_SETS))
      return VK_SUCCESS;

   bool dirty_lrz =
      (dirty & TU_CMD_DIRTY_LRZ) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_STENCIL_OP) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
      BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                  MESA_VK_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE);

   if (dirty_lrz) {
      struct tu_cs cs;
      uint32_t size = 8 +
                      (cmd->device->physical_device->info->props.lrz_track_quirk ? 2 : 0) +
                      (CHIP >= A7XX ? 2 : 0); // A7XX has extra packets from LRZ_CNTL2.

      cmd->state.lrz_and_depth_plane_state =
         tu_cs_draw_state(&cmd->sub_cs, &cs, size);
      tu6_update_simplified_stencil_state(cmd);
      tu6_emit_lrz<CHIP>(cmd, &cs);
      tu6_build_depth_plane_z_mode<CHIP>(cmd, &cs);
   }

   if (BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE)) {
      if (cmd->vk.dynamic_graphics_state.feedback_loops &&
          !cmd->state.rp.disable_gmem) {
         perf_debug(
            cmd->device,
            "Disabling gmem due to VK_EXT_attachment_feedback_loop_layout");
         cmd->state.rp.disable_gmem = true;
         cmd->state.rp.gmem_disable_reason =
            "MESA_VK_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE";
      }
   }

   if (BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_VI_BINDINGS_VALID)) {
      cmd->state.vertex_buffers.size =
         util_last_bit(cmd->vk.dynamic_graphics_state.vi_bindings_valid) * 4;
      dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;
   }

   if (dirty & TU_CMD_DIRTY_SHADER_CONSTS)
      cmd->state.shader_const = tu_emit_consts(cmd, false);

   if (dirty & TU_CMD_DIRTY_DESC_SETS)
      tu6_emit_descriptor_sets<CHIP>(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

   if (BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
       BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY) ||
       BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_RS_LINE_MODE) ||
       (cmd->state.dirty & TU_CMD_DIRTY_TES) ||
       (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      tu6_update_msaa_disable<CHIP>(cmd);
   }

   if (BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
       (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      tu6_update_msaa<CHIP>(cmd);
   }

   bool dirty_fs_params = false;
   if (BITSET_TEST(cmd->vk.dynamic_graphics_state.dirty,
                   MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
       (cmd->state.dirty & (TU_CMD_DIRTY_PROGRAM | TU_CMD_DIRTY_FDM))) {
      tu_emit_fs_params(cmd);
      dirty_fs_params = true;
   }

   /* for the first draw in a renderpass, re-emit all the draw states
    *
    * and if a draw-state disabling path (CmdClearAttachments 3D fallback) was
    * used, then draw states must be re-emitted. note however this only happens
    * in the sysmem path, so this can be skipped this for the gmem path (TODO)
    *
    * the two input attachment states are excluded because secondary command
    * buffer doesn't have a state ib to restore it, and not re-emitting them
    * is OK since CmdClearAttachments won't disable/overwrite them
    */
   if (dirty & TU_CMD_DIRTY_DRAW_STATE) {
      tu_pipeline_update_rp_state(&cmd->state);

      tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (TU_DRAW_STATE_COUNT - 2));

      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_CONFIG, program->config_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS, program->vs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_BINNING, program->vs_binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_HS, program->hs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DS, program->ds_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_GS, program->gs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_GS_BINNING, program->gs_binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_FS, program->fs_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VPC, program->vpc_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PRIM_MODE_GMEM, cmd->state.prim_order_gmem);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_CONST, cmd->state.shader_const);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS, cmd->state.desc_sets);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS_LOAD, cmd->state.load_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_FS_PARAMS, cmd->state.fs_params);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_LRZ_AND_DEPTH_PLANE, cmd->state.lrz_and_depth_plane_state);

      for (uint32_t i = 0; i < ARRAY_SIZE(cmd->state.dynamic_state); i++) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i,
                               cmd->state.dynamic_state[i]);
      }
   } else {
      /* emit draw states that were just updated */
      uint32_t draw_state_count =
         util_bitcount(dynamic_draw_state_dirty) +
         ((dirty & TU_CMD_DIRTY_SHADER_CONSTS) ? 1 : 0) +
         ((dirty & TU_CMD_DIRTY_DESC_SETS) ? 1 : 0) +
         ((dirty & TU_CMD_DIRTY_VERTEX_BUFFERS) ? 1 : 0) +
         ((dirty & TU_CMD_DIRTY_VS_PARAMS) ? 1 : 0) +
         (dirty_fs_params ? 1 : 0) +
         (dirty_lrz ? 1 : 0);

      if (draw_state_count > 0)
         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * draw_state_count);

      if (dirty & TU_CMD_DIRTY_SHADER_CONSTS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_CONST, cmd->state.shader_const);
      if (dirty & TU_CMD_DIRTY_DESC_SETS) {
         /* tu6_emit_descriptor_sets emitted the cmd->state.desc_sets draw state. */
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS_LOAD, cmd->state.load_state);
      }
      if (dirty & TU_CMD_DIRTY_VERTEX_BUFFERS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers);
      u_foreach_bit (i, dynamic_draw_state_dirty) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i,
                               cmd->state.dynamic_state[i]);
      }
      if (dirty & TU_CMD_DIRTY_VS_PARAMS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
      if (dirty_fs_params)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_FS_PARAMS, cmd->state.fs_params);
      if (dirty_lrz) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_LRZ_AND_DEPTH_PLANE, cmd->state.lrz_and_depth_plane_state);
      }
   }

   tu_cs_sanity_check(cs);

   /* There are too many graphics dirty bits to list here, so just list the
    * bits to preserve instead. The only things not emitted here are
    * compute-related state.
    */
   cmd->state.dirty &= TU_CMD_DIRTY_COMPUTE_DESC_SETS;
   BITSET_ZERO(cmd->vk.dynamic_graphics_state.dirty);
   return VK_SUCCESS;
}

static uint32_t
tu_draw_initiator(struct tu_cmd_buffer *cmd, enum pc_di_src_sel src_sel)
{
   enum pc_di_primtype primtype =
      tu6_primtype((VkPrimitiveTopology)cmd->vk.dynamic_graphics_state.ia.primitive_topology);

   if (primtype == DI_PT_PATCHES0)
      primtype = (enum pc_di_primtype) (primtype +
                                        cmd->vk.dynamic_graphics_state.ts.patch_control_points);

   uint32_t initiator =
      CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(primtype) |
      CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(src_sel) |
      CP_DRAW_INDX_OFFSET_0_INDEX_SIZE((enum a4xx_index_size) cmd->state.index_size) |
      CP_DRAW_INDX_OFFSET_0_VIS_CULL(USE_VISIBILITY);

   if (cmd->state.shaders[MESA_SHADER_GEOMETRY]->variant)
      initiator |= CP_DRAW_INDX_OFFSET_0_GS_ENABLE;

   const struct tu_shader *tes = cmd->state.shaders[MESA_SHADER_TESS_EVAL];
   if (tes->variant) {
      switch (tes->variant->key.tessellation) {
      case IR3_TESS_TRIANGLES:
         initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_TRIANGLES) |
                      CP_DRAW_INDX_OFFSET_0_TESS_ENABLE;
         break;
      case IR3_TESS_ISOLINES:
         initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_ISOLINES) |
                      CP_DRAW_INDX_OFFSET_0_TESS_ENABLE;
         break;
      case IR3_TESS_QUADS:
         initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_QUADS) |
                      CP_DRAW_INDX_OFFSET_0_TESS_ENABLE;
         break;
      }
   }
   return initiator;
}


static uint32_t
vs_params_offset(struct tu_cmd_buffer *cmd)
{
   const struct tu_program_descriptor_linkage *link =
      &cmd->state.program.link[MESA_SHADER_VERTEX];
   const struct ir3_const_state *const_state = &link->const_state;

   uint32_t param_offset =
      const_state->allocs.consts[IR3_CONST_ALLOC_DRIVER_PARAMS].offset_vec4;

   if (!ir3_const_can_upload(&const_state->allocs,
                             IR3_CONST_ALLOC_DRIVER_PARAMS, link->constlen))
      return 0;

   /* this layout is required by CP_DRAW_INDIRECT_MULTI */
   STATIC_ASSERT(IR3_DP_VS(draw_id) == 0);
   STATIC_ASSERT(IR3_DP_VS(vtxid_base) == 1);
   STATIC_ASSERT(IR3_DP_VS(instid_base) == 2);

   /* 0 means disabled for CP_DRAW_INDIRECT_MULTI */
   assert(param_offset != 0);

   return param_offset;
}

template <chip CHIP>
static void
tu6_emit_empty_vs_params(struct tu_cmd_buffer *cmd)
{
   if (cmd->state.last_vs_params.empty)
      return;

   if (cmd->device->physical_device->info->props.load_shader_consts_via_preamble) {
      struct tu_cs cs;
      cmd->state.vs_params = tu_cs_draw_state(&cmd->sub_cs, &cs, 2);

      /* CP_LOAD_STATE6_GEOM from previous draws can override consts loaded for
       * indirect draws, causing problems like incorrect vertex index computation.
       * VS state invalidation avoids that.
       */
      tu_cs_emit_regs(&cs, SP_UPDATE_CNTL(CHIP,
         .vs_state = true));
      assert(cs.cur == cs.end);
   } else {
      cmd->state.vs_params = (struct tu_draw_state) {};
   }
   cmd->state.dirty |= TU_CMD_DIRTY_VS_PARAMS;

   cmd->state.last_vs_params.empty = true;
}

static void
tu6_emit_vs_params(struct tu_cmd_buffer *cmd,
                   uint32_t draw_id,
                   uint32_t vertex_offset,
                   uint32_t first_instance)
{
   uint32_t offset = vs_params_offset(cmd);

   /* Beside re-emitting params when they are changed, we should re-emit
    * them after constants are invalidated via SP_UPDATE_CNTL or after we
    * emit an empty vs params.
    */
   if (!(cmd->state.dirty & (TU_CMD_DIRTY_DRAW_STATE | TU_CMD_DIRTY_VS_PARAMS |
                             TU_CMD_DIRTY_PROGRAM)) &&
       !cmd->state.last_vs_params.empty &&
       (offset == 0 || draw_id == cmd->state.last_vs_params.draw_id) &&
       vertex_offset == cmd->state.last_vs_params.vertex_offset &&
       first_instance == cmd->state.last_vs_params.first_instance) {
      return;
   }

   uint64_t consts_iova = 0;
   if (offset) {
      struct tu_cs_memory consts;
      VkResult result = tu_cs_alloc(&cmd->sub_cs, 1, 4, &consts);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }
      consts.map[0] = draw_id;
      consts.map[1] = vertex_offset;
      consts.map[2] = first_instance;
      consts.map[3] = 0;

      consts_iova = consts.iova;
   }

   struct tu_cs cs;
   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 3 + (offset ? 4 : 0), &cs);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_cs_emit_regs(&cs,
                   A6XX_VFD_INDEX_OFFSET(vertex_offset),
                   A6XX_VFD_INSTANCE_START_OFFSET(first_instance));

   /* It is implemented as INDIRECT load even on a750+ because with UBO
    * lowering it would be tricky to get const offset for to use in multidraw,
    * also we would need to ensure the offset is not 0.
    * TODO/A7XX: Rework vs params to use UBO lowering.
    */
   if (offset) {
      tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_GEOM, 3);
      tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(SB6_VS_SHADER) |
            CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit_qw(&cs, consts_iova);
   }

   cmd->state.last_vs_params.vertex_offset = vertex_offset;
   cmd->state.last_vs_params.first_instance = first_instance;
   cmd->state.last_vs_params.draw_id = draw_id;
   cmd->state.last_vs_params.empty = false;

   struct tu_cs_entry entry = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
   cmd->state.vs_params = (struct tu_draw_state) {entry.bo->iova + entry.offset, entry.size / 4};

   cmd->state.dirty |= TU_CMD_DIRTY_VS_PARAMS;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDraw(VkCommandBuffer commandBuffer,
           uint32_t vertexCount,
           uint32_t instanceCount,
           uint32_t firstVertex,
           uint32_t firstInstance)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_vs_params(cmd, 0, firstVertex, firstInstance);

   tu6_draw_common<CHIP>(cmd, cs, false, vertexCount);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit(cs, vertexCount);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDraw);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawMultiEXT(VkCommandBuffer commandBuffer,
                   uint32_t drawCount,
                   const VkMultiDrawInfoEXT *pVertexInfo,
                   uint32_t instanceCount,
                   uint32_t firstInstance,
                   uint32_t stride)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   if (!drawCount)
      return;

   bool has_tess = cmd->state.shaders[MESA_SHADER_TESS_CTRL]->variant;

   uint32_t max_vertex_count = 0;
   if (has_tess) {
      uint32_t i = 0;
      vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
         max_vertex_count = MAX2(max_vertex_count, draw->vertexCount);
      }
   }

   uint32_t i = 0;
   vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
      tu6_emit_vs_params(cmd, i, draw->firstVertex, firstInstance);

      if (i == 0)
         tu6_draw_common<CHIP>(cmd, cs, false, max_vertex_count);

      if (cmd->state.dirty & TU_CMD_DIRTY_VS_PARAMS) {
         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
         cmd->state.dirty &= ~TU_CMD_DIRTY_VS_PARAMS;
      }

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
      tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
      tu_cs_emit(cs, instanceCount);
      tu_cs_emit(cs, draw->vertexCount);
   }

   if (i != 0)
      trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawMultiEXT);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                  uint32_t indexCount,
                  uint32_t instanceCount,
                  uint32_t firstIndex,
                  int32_t vertexOffset,
                  uint32_t firstInstance)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_vs_params(cmd, 0, vertexOffset, firstInstance);

   tu6_draw_common<CHIP>(cmd, cs, true, indexCount);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 7);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit(cs, indexCount);
   tu_cs_emit(cs, firstIndex);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawIndexed);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer,
                          uint32_t drawCount,
                          const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                          uint32_t instanceCount,
                          uint32_t firstInstance,
                          uint32_t stride,
                          const int32_t *pVertexOffset)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   if (!drawCount)
      return;

   bool has_tess = cmd->state.shaders[MESA_SHADER_TESS_CTRL]->variant;

   uint32_t max_index_count = 0;
   if (has_tess) {
      uint32_t i = 0;
      vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
         max_index_count = MAX2(max_index_count, draw->indexCount);
      }
   }

   uint32_t i = 0;
   vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
      int32_t vertexOffset = pVertexOffset ? *pVertexOffset : draw->vertexOffset;
      tu6_emit_vs_params(cmd, i, vertexOffset, firstInstance);

      if (i == 0)
         tu6_draw_common<CHIP>(cmd, cs, true, max_index_count);

      if (cmd->state.dirty & TU_CMD_DIRTY_VS_PARAMS) {
         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
         cmd->state.dirty &= ~TU_CMD_DIRTY_VS_PARAMS;
      }

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 7);
      tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
      tu_cs_emit(cs, instanceCount);
      tu_cs_emit(cs, draw->indexCount);
      tu_cs_emit(cs, draw->firstIndex);
      tu_cs_emit_qw(cs, cmd->state.index_va);
      tu_cs_emit(cs, cmd->state.max_index_count);
   }

   if (i != 0)
      trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawMultiIndexedEXT);

/* Various firmware bugs/inconsistencies mean that some indirect draw opcodes
 * do not wait for WFI's to complete before executing. Add a WAIT_FOR_ME if
 * pending for these opcodes. This may result in a few extra WAIT_FOR_ME's
 * with these opcodes, but the alternative would add unnecessary WAIT_FOR_ME's
 * before draw opcodes that don't need it.
 */
static void
draw_wfm(struct tu_cmd_buffer *cmd)
{
   cmd->state.renderpass_cache.flush_bits |=
      cmd->state.renderpass_cache.pending_flush_bits & TU_CMD_FLAG_WAIT_FOR_ME;
   cmd->state.renderpass_cache.pending_flush_bits &= ~TU_CMD_FLAG_WAIT_FOR_ME;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                   VkBuffer _buffer,
                   VkDeviceSize offset,
                   uint32_t drawCount,
                   uint32_t stride)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buf, _buffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params<CHIP>(cmd);

   if (cmd->device->physical_device->info->props.indirect_draw_wfm_quirk)
      draw_wfm(cmd);

   tu6_draw_common<CHIP>(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_NORMAL) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, offset));
   tu_cs_emit(cs, stride);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawIndirect);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                          VkBuffer _buffer,
                          VkDeviceSize offset,
                          uint32_t drawCount,
                          uint32_t stride)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buf, _buffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params<CHIP>(cmd);

   if (cmd->device->physical_device->info->props.indirect_draw_wfm_quirk)
      draw_wfm(cmd);

   tu6_draw_common<CHIP>(cmd, cs, true, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 9);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, offset));
   tu_cs_emit(cs, stride);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawIndexedIndirect);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                        VkBuffer _buffer,
                        VkDeviceSize offset,
                        VkBuffer countBuffer,
                        VkDeviceSize countBufferOffset,
                        uint32_t drawCount,
                        uint32_t stride)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buf, _buffer);
   VK_FROM_HANDLE(tu_buffer, count_buf, countBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params<CHIP>(cmd);

   /* It turns out that the firmware we have for a650 only partially fixed the
    * problem with CP_DRAW_INDIRECT_MULTI not waiting for WFI's to complete
    * before reading indirect parameters. It waits for WFI's before reading
    * the draw parameters, but after reading the indirect count :(.
    */
   draw_wfm(cmd);

   tu6_draw_common<CHIP>(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 8);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDIRECT_COUNT) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, offset));
   tu_cs_emit_qw(cs, vk_buffer_address(&count_buf->vk, countBufferOffset));
   tu_cs_emit(cs, stride);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawIndirectCount);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer,
                               VkBuffer _buffer,
                               VkDeviceSize offset,
                               VkBuffer countBuffer,
                               VkDeviceSize countBufferOffset,
                               uint32_t drawCount,
                               uint32_t stride)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buf, _buffer);
   VK_FROM_HANDLE(tu_buffer, count_buf, countBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params<CHIP>(cmd);

   draw_wfm(cmd);

   tu6_draw_common<CHIP>(cmd, cs, true, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 11);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDIRECT_COUNT_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, offset));
   tu_cs_emit_qw(cs, vk_buffer_address(&count_buf->vk, countBufferOffset));
   tu_cs_emit(cs, stride);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawIndexedIndirectCount);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer,
                               uint32_t instanceCount,
                               uint32_t firstInstance,
                               VkBuffer _counterBuffer,
                               VkDeviceSize counterBufferOffset,
                               uint32_t counterOffset,
                               uint32_t vertexStride)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buf, _counterBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   /* All known firmware versions do not wait for WFI's with CP_DRAW_AUTO.
    * Plus, for the common case where the counter buffer is written by
    * vkCmdEndTransformFeedback, we need to wait for the CP_WAIT_MEM_WRITES to
    * complete which means we need a WAIT_FOR_ME anyway.
    */
   draw_wfm(cmd);

   tu6_emit_vs_params(cmd, 0, 0, firstInstance);

   tu6_draw_common<CHIP>(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_AUTO, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_XFB));
   if (CHIP >= A7XX) {
      /* On a7xx the counter value and offset are shifted right by 2, so
       * the vertexStride should also be in units of dwords.
       */
      vertexStride = vertexStride >> 2;
   }
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit_qw(cs, vk_buffer_address(&buf->vk, counterBufferOffset));
   tu_cs_emit(cs, counterOffset);
   tu_cs_emit(cs, vertexStride);

   trace_end_draw(&cmd->rp_trace, cs);
}
TU_GENX(tu_CmdDrawIndirectByteCountEXT);

struct tu_dispatch_info
{
   /**
    * Determine the layout of the grid (in block units) to be used.
    */
   uint32_t blocks[3];

   /**
    * A starting offset for the grid. If unaligned is set, the offset
    * must still be aligned.
    */
   uint32_t offsets[3];
   /**
    * Whether it's an unaligned compute dispatch.
    */
   bool unaligned;

   /**
    * Indirect compute parameters resource.
    */
   VkDeviceAddress indirect;
};

static inline struct ir3_driver_params_cs
build_driver_params_cs(const struct ir3_shader_variant *variant,
                       const struct tu_dispatch_info *info)
{
   unsigned subgroup_size = variant->info.subgroup_size;
   unsigned subgroup_shift = util_logbase2(subgroup_size);

   return (struct ir3_driver_params_cs) {
      .num_work_groups_x = info->blocks[0],
      .num_work_groups_y = info->blocks[1],
      .num_work_groups_z = info->blocks[2],
      .work_dim = 0,
      .base_group_x = info->offsets[0],
      .base_group_y = info->offsets[1],
      .base_group_z = info->offsets[2],
      .subgroup_size = subgroup_size,
      .local_group_size_x = 0,
      .local_group_size_y = 0,
      .local_group_size_z = 0,
      .subgroup_id_shift = subgroup_shift,
   };
}

template <chip CHIP>
static void
tu_emit_compute_driver_params(struct tu_cmd_buffer *cmd,
                              struct tu_cs *cs,
                              const struct tu_dispatch_info *info)
{
   mesa_shader_stage type = MESA_SHADER_COMPUTE;
   const struct tu_shader *shader = cmd->state.shaders[MESA_SHADER_COMPUTE];
   const struct ir3_shader_variant *variant = shader->variant;
   const struct ir3_const_state *const_state = variant->const_state;
   unsigned subgroup_size = variant->info.subgroup_size;
   unsigned subgroup_shift = util_logbase2(subgroup_size);

   if (cmd->device->physical_device->info->props.load_shader_consts_via_preamble) {
      uint32_t num_consts = const_state->driver_params_ubo.size;
      if (num_consts == 0)
         return;

      bool direct_indirect_load =
         !(info->indirect & 0xf) &&
         !(info->indirect && num_consts > IR3_DP_CS(base_group_x));

      uint64_t iova = 0;

      if (!info->indirect) {
         struct ir3_driver_params_cs driver_params =
            build_driver_params_cs(variant, info);

         assert(num_consts <= dword_sizeof(driver_params));

         struct tu_cs_memory consts;
         uint32_t consts_vec4 = DIV_ROUND_UP(num_consts, 4);
         VkResult result = tu_cs_alloc(&cmd->sub_cs, consts_vec4, 4, &consts);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(&cmd->vk, result);
            return;
         }
         memcpy(consts.map, &driver_params, num_consts * sizeof(uint32_t));
         iova = consts.iova;
      } else if (direct_indirect_load) {
         iova = info->indirect;
      } else {
         /* Vulkan guarantees only 4 byte alignment for indirect_offset.
          * However, CP_LOAD_STATE.EXT_SRC_ADDR needs 16 byte alignment.
          */

         uint64_t indirect_iova = info->indirect;

         /* Wait for any previous uses to finish. */
         tu_cs_emit_wfi(cs);

         for (uint32_t i = 0; i < 3; i++) {
            tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
            tu_cs_emit(cs, 0);
            tu_cs_emit_qw(cs, global_iova_arr(cmd, cs_indirect_xyz, i));
            tu_cs_emit_qw(cs, indirect_iova + i * sizeof(uint32_t));
         }

         /* Fill out IR3_DP_CS_SUBGROUP_SIZE and IR3_DP_SUBGROUP_ID_SHIFT for
          * indirect dispatch.
          */
         if (info->indirect && num_consts > IR3_DP_CS(base_group_x)) {
            uint32_t indirect_driver_params[8] = {
               0, 0, 0, subgroup_size,
               0, 0, 0, subgroup_shift,
            };
            bool emit_local = num_consts > IR3_DP_CS(local_group_size_x);
            uint32_t emit_size = emit_local ? 8 : 4;

            tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 2 + emit_size);
            tu_cs_emit_qw(cs, global_iova_arr(cmd, cs_indirect_xyz, 0) + 4 * sizeof(uint32_t));
            for (uint32_t i = 0; i < emit_size; i++) {
               tu_cs_emit(cs, indirect_driver_params[i]);
            }
         }

         tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
         tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);
         tu_cs_emit_wfi(cs);

         iova = global_iova(cmd, cs_indirect_xyz[0]);
      }

      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 5);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(const_state->driver_params_ubo.idx) |
               CP_LOAD_STATE6_0_STATE_TYPE(ST6_UBO) |
               CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
               CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
               CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
      tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
      int size_vec4s = DIV_ROUND_UP(num_consts, 4);
      tu_cs_emit_qw(cs, iova | ((uint64_t)A6XX_UBO_1_SIZE(size_vec4s) << 32));

   } else {
      uint32_t offset =
         const_state->allocs.consts[IR3_CONST_ALLOC_DRIVER_PARAMS].offset_vec4;
      if (!ir3_const_can_upload(&const_state->allocs,
                                IR3_CONST_ALLOC_DRIVER_PARAMS,
                                variant->constlen))
         return;

      uint32_t num_consts = MIN2(const_state->num_driver_params,
                                 (variant->constlen - offset) * 4);

      if (!info->indirect) {
         struct ir3_driver_params_cs driver_params =
            build_driver_params_cs(variant, info);

         assert(num_consts <= dword_sizeof(driver_params));

         /* push constants */
         tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3 + num_consts);
         tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                  CP_LOAD_STATE6_0_NUM_UNIT(num_consts / 4));
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
         tu_cs_emit_array(cs, (uint32_t *)&driver_params, num_consts);
      } else if (!(info->indirect & 0xf)) {
         tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3);
         tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                     CP_LOAD_STATE6_0_NUM_UNIT(1));
         tu_cs_emit_qw(cs, info->indirect);
      } else {
         /* Vulkan guarantees only 4 byte alignment for indirect_offset.
          * However, CP_LOAD_STATE.EXT_SRC_ADDR needs 16 byte alignment.
          */

         /* Wait for any previous uses to finish. */
         tu_cs_emit_wfi(cs);

         for (uint32_t i = 0; i < 3; i++) {
            tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
            tu_cs_emit(cs, 0);
            tu_cs_emit_qw(cs, global_iova_arr(cmd, cs_indirect_xyz, i));
            tu_cs_emit_qw(cs, info->indirect + i * 4);
         }

         tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
         tu_emit_event_write<CHIP>(cmd, cs, FD_CACHE_INVALIDATE);
         tu_cs_emit_wfi(cs);

         tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3);
         tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                     CP_LOAD_STATE6_0_NUM_UNIT(1));
         tu_cs_emit_qw(cs, global_iova(cmd, cs_indirect_xyz[0]));
      }

      /* Fill out IR3_DP_CS_SUBGROUP_SIZE and IR3_DP_SUBGROUP_ID_SHIFT for
       * indirect dispatch.
       */
      if (info->indirect && num_consts > IR3_DP_CS(base_group_x)) {
         bool emit_local = num_consts > IR3_DP_CS(local_group_size_x);
         tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 7 + (emit_local ? 4 : 0));
         tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset + (IR3_DP_CS(base_group_x) / 4)) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                  CP_LOAD_STATE6_0_NUM_UNIT((num_consts - IR3_DP_CS(base_group_x)) / 4));
         tu_cs_emit_qw(cs, 0);
         tu_cs_emit(cs, 0); /* BASE_GROUP_X */
         tu_cs_emit(cs, 0); /* BASE_GROUP_Y */
         tu_cs_emit(cs, 0); /* BASE_GROUP_Z */
         tu_cs_emit(cs, subgroup_size);
         if (emit_local) {
            assert(num_consts == align(IR3_DP_CS(subgroup_id_shift), 4));
            tu_cs_emit(cs, 0); /* LOCAL_GROUP_SIZE_X */
            tu_cs_emit(cs, 0); /* LOCAL_GROUP_SIZE_Y */
            tu_cs_emit(cs, 0); /* LOCAL_GROUP_SIZE_Z */
            tu_cs_emit(cs, subgroup_shift);
         }
      }
   }
}

template <chip CHIP>
static void
tu_dispatch(struct tu_cmd_buffer *cmd,
            const struct tu_dispatch_info *info)
{
   if (!info->indirect &&
       (info->blocks[0] == 0 || info->blocks[1] == 0 || info->blocks[2] == 0))
      return;

   struct tu_cs *cs = &cmd->cs;
   struct tu_shader *shader = cmd->state.shaders[MESA_SHADER_COMPUTE];

   bool emit_instrlen_workaround =
      shader->variant->instrlen >
      cmd->device->physical_device->info->props.instr_cache_size;

   /* We don't use draw states for dispatches, so the bound pipeline
    * could be overwritten by reg stomping in a renderpass or blit.
    */
   if (cmd->device->dbg_renderpass_stomp_cs) {
      tu_cs_emit_state_ib(&cmd->cs, shader->state);
   }

   /* There appears to be a HW bug where in some rare circumstances it appears
    * to accidentally use the FS instrlen instead of the CS instrlen, which
    * affects all known gens. Based on various experiments it appears that the
    * issue is that when prefetching a branch destination and there is a cache
    * miss, when fetching from memory the HW bounds-checks the fetch against
    * SP_CS_INSTR_SIZE, except when one of the two register contexts is active
    * it accidentally fetches SP_PS_INSTR_SIZE from the other (inactive)
    * context. To workaround it we set the FS instrlen here and do a dummy
    * event to roll the context (because it fetches SP_PS_INSTR_SIZE from the
    * "wrong" context). Because the bug seems to involve cache misses, we
    * don't emit this if the entire CS program fits in cache, which will
    * hopefully be the majority of cases.
    *
    * See https://gitlab.freedesktop.org/mesa/mesa/-/issues/5892
    */
   if (emit_instrlen_workaround) {
      tu_cs_emit_regs(cs, A6XX_SP_PS_INSTR_SIZE(shader->variant->instrlen));
      tu_emit_event_write<CHIP>(cmd, cs, FD_LABEL);
   }

   /* TODO: We could probably flush less if we add a compute_flush_bits
    * bitfield.
    */
   tu_emit_cache_flush<CHIP>(cmd);

   /* note: no reason to have this in a separate IB */
   tu_cs_emit_state_ib(cs, tu_emit_consts(cmd, true));

   tu_emit_compute_driver_params<CHIP>(cmd, cs, info);

   if (cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_DESC_SETS) {
      tu6_emit_descriptor_sets<CHIP>(cmd, VK_PIPELINE_BIND_POINT_COMPUTE);
      tu_cs_emit_state_ib(cs, cmd->state.compute_load_state);
   }

   cmd->state.dirty &= ~TU_CMD_DIRTY_COMPUTE_DESC_SETS;

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_COMPUTE));

   const uint16_t *local_size = shader->variant->local_size;
   const uint32_t *num_groups = info->blocks;

   if (info->unaligned) {
      assert(CHIP >= A7XX);

      if (info->indirect) {
         /* This path is tailored for BVH building and currently only supports
          * 1-dimensional dispatches with a power-of-two local size. We use
          * CP_RUN_OPENCL instead of CP_EXEC_CS in order to dynamically set
          * SP_CS_KERNEL_GROUP_X, which is usually set implicitly by the
          * packet, to the number of workgroups. The registers for Y and Z
          * dimensions should be unused because we set the kernel dimension to
          * 1.
          */
         assert(local_size[1] == 1 && local_size[2] == 1);
         assert(util_is_power_of_two_nonzero(local_size[0]));

         tu_cs_emit_regs(cs,
                         SP_CS_NDRANGE_0(CHIP, .kerneldim = 1,
                                                 .localsizex = local_size[0] - 1));

         tu_cs_emit_regs(cs, SP_CS_NDRANGE_2(CHIP, .globaloff_x = 0));

         /* This does:
          * - waits for pending cache flushes to finish
          * - CP_WAIT_FOR_ME
          *
          * In a sequence of indirect dispatches this shouldn't wait for the
          * previous dispatches to finish.
          */
         tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
         tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A7XX_SP_CS_NDRANGE_1));
         tu_cs_emit_qw(cs, info->indirect);

         tu_cs_emit_pkt7(cs, CP_SCRATCH_WRITE, 2);
         tu_cs_emit(cs, CP_SCRATCH_WRITE_0_SCRATCH(0));
         tu_cs_emit(cs, ~0u);

         /* CP_REG_RMW and CP_REG_TO_SCRATCH implicitly do a CP_WAIT_FOR_IDLE
          * *and* CP_WAIT_FOR_ME, which is a full pipeline stall that we don't
          * want, so manually wait for the CP_MEM_TO_REG write to land and
          * then skip waiting below with SKIP_WAIT_FOR_ME.
          */
         tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

         /* scratch0 = ((scratch0 & CS_NDRANGE_1) + -1
          *          = ((~0 & CS_NDRANGE_1) + -1
          *          =  CS_NDRANGE_1 - 1
          */ 
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs,
                    CP_REG_RMW_0_DST_REG(0) |
                    CP_REG_RMW_0_DST_SCRATCH |
                    CP_REG_RMW_0_SKIP_WAIT_FOR_ME |
                    CP_REG_RMW_0_SRC0_IS_REG |
                    CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit(cs, REG_A7XX_SP_CS_NDRANGE_1); /* SRC0 */
         tu_cs_emit(cs, -1); /* SRC1 */

         /* scratch0 = ((scratch0 & (local_size - 1)) rot 2
          *          = ((scratch0 & (local_size - 1)) << 2
          */ 
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs,
                    CP_REG_RMW_0_DST_REG(0) |
                    CP_REG_RMW_0_DST_SCRATCH |
                    CP_REG_RMW_0_SKIP_WAIT_FOR_ME |
                    CP_REG_RMW_0_ROTATE(A7XX_SP_CS_NDRANGE_7_LOCALSIZEX__SHIFT));
         tu_cs_emit(cs, local_size[0] - 1); /* SRC0 */
         tu_cs_emit(cs, 0); /* SRC1 */

         /* write scratch0 to SP_CS_NDRANGE_7 */
         tu_cs_emit_pkt7(cs, CP_SCRATCH_TO_REG, 1);
         tu_cs_emit(cs,
                    CP_SCRATCH_TO_REG_0_REG(REG_A7XX_SP_CS_NDRANGE_7) |
                    CP_SCRATCH_TO_REG_0_SCRATCH(0));

         tu_cs_emit_pkt7(cs, CP_SCRATCH_WRITE, 2);
         tu_cs_emit(cs, CP_SCRATCH_WRITE_0_SCRATCH(0));
         tu_cs_emit(cs, ~0u);

         /* scratch0 = (scratch0 & CS_NDRANGE_1) + local_size - 1
          *          = (~0u & CS_NDRANGE_1) + local_size - 1
          *          = CS_NDRANGE_1 + local_size - 1
          */
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs,
                    CP_REG_RMW_0_DST_REG(0) |
                    CP_REG_RMW_0_DST_SCRATCH |
                    CP_REG_RMW_0_SKIP_WAIT_FOR_ME |
                    CP_REG_RMW_0_SRC0_IS_REG |
                    CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit(cs, REG_A7XX_SP_CS_NDRANGE_1); /* SRC0 */
         tu_cs_emit(cs, local_size[0] - 1); /* SRC1 */

         unsigned local_size_log2 = util_logbase2(local_size[0]);

         /* scratch0 = (scratch0 & (~(local_size - 1)) rot (32 - log2(local_size))
          *          = scratch0 >> log2(local_size)
          *          = scratch0 / local_size
          *          = (CS_NDRANGE_1 + local_size - 1) / local_size
          */
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs,
                    CP_REG_RMW_0_DST_REG(0) |
                    CP_REG_RMW_0_DST_SCRATCH |
                    CP_REG_RMW_0_SKIP_WAIT_FOR_ME |
                    CP_REG_RMW_0_ROTATE(32 - local_size_log2));
         tu_cs_emit(cs, ~(local_size[0] - 1)); /* SRC0 */
         tu_cs_emit(cs, 0); /* SRC1 */

         /* write scratch0 to SP_CS_KERNEL_GROUP_X */
         tu_cs_emit_pkt7(cs, CP_SCRATCH_TO_REG, 1);
         tu_cs_emit(cs,
                    CP_SCRATCH_TO_REG_0_REG(REG_A7XX_SP_CS_KERNEL_GROUP_X) |
                    CP_SCRATCH_TO_REG_0_SCRATCH(0));
      } else {
         tu_cs_emit_regs(cs,
                         SP_CS_NDRANGE_0(CHIP, .kerneldim = 3,
                                                 .localsizex = local_size[0] - 1,
                                                 .localsizey = local_size[1] - 1,
                                                 .localsizez = local_size[2] - 1),
                         SP_CS_NDRANGE_1(CHIP, .globalsize_x = num_groups[0]),
                         SP_CS_NDRANGE_2(CHIP, .globaloff_x = 0),
                         SP_CS_NDRANGE_3(CHIP, .globalsize_y = num_groups[1]),
                         SP_CS_NDRANGE_4(CHIP, .globaloff_y = 0),
                         SP_CS_NDRANGE_5(CHIP, .globalsize_z = num_groups[2]),
                         SP_CS_NDRANGE_6(CHIP, .globaloff_z = 0));
         uint32_t last_local_size[3];
         for (unsigned i = 0; i < 3; i++)
            last_local_size[i] = ((num_groups[i] - 1) % local_size[i]) + 1;
         tu_cs_emit_regs(cs,
                         SP_CS_NDRANGE_7(CHIP, .localsizex = last_local_size[0] - 1,
                                                      .localsizey = last_local_size[1] - 1,
                                                      .localsizez = last_local_size[2] - 1));
      }
   } else {
      tu_cs_emit_regs(cs,
                      SP_CS_NDRANGE_0(CHIP, .kerneldim = 3,
                                              .localsizex = local_size[0] - 1,
                                              .localsizey = local_size[1] - 1,
                                              .localsizez = local_size[2] - 1),
                      SP_CS_NDRANGE_1(CHIP, .globalsize_x = local_size[0] * num_groups[0]),
                      SP_CS_NDRANGE_2(CHIP, .globaloff_x = 0),
                      SP_CS_NDRANGE_3(CHIP, .globalsize_y = local_size[1] * num_groups[1]),
                      SP_CS_NDRANGE_4(CHIP, .globaloff_y = 0),
                      SP_CS_NDRANGE_5(CHIP, .globalsize_z = local_size[2] * num_groups[2]),
                      SP_CS_NDRANGE_6(CHIP, .globaloff_z = 0));
      if (CHIP >= A7XX) {
         tu_cs_emit_regs(cs,
                         SP_CS_NDRANGE_7(CHIP, .localsizex = local_size[0] - 1,
                                                      .localsizey = local_size[1] - 1,
                                                      .localsizez = local_size[2] - 1));
      }
   }

   if (cmd->device->physical_device->info->props.has_rt_workaround &&
       shader->variant->info.uses_ray_intersection) {
      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_SHADER_USES_RT);
   }

   if (info->indirect) {
      trace_start_compute_indirect(&cmd->trace, cs, cmd, info->unaligned,
                                   (char *)shader->variant->sha1_str);

      if (info->unaligned) {
         tu_cs_emit_pkt7(cs, CP_RUN_OPENCL, 1);
         tu_cs_emit(cs, 0x00000000);
      } else {
         tu_cs_emit_pkt7(cs, CP_EXEC_CS_INDIRECT, 4);
         tu_cs_emit(cs, 0x00000000);
         tu_cs_emit_qw(cs, info->indirect);
         tu_cs_emit(cs,
                    A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEX(local_size[0] - 1) |
                    A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEY(local_size[1] - 1) |
                    A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEZ(local_size[2] - 1));

      }

      trace_end_compute_indirect(&cmd->trace, cs,
                                 (struct u_trace_address) {
                                    .bo = NULL,
                                    .offset = info->indirect,
                                 });
   } else {
      trace_start_compute(&cmd->trace, cs, cmd, info->indirect != 0,
                          info->unaligned, local_size[0], local_size[1],
                          local_size[2], info->blocks[0], info->blocks[1],
                          info->blocks[2], (char *)shader->variant->sha1_str);

      if (info->unaligned) {
         tu_cs_emit_pkt7(cs, CP_EXEC_CS, 4);
         tu_cs_emit(cs, 0x00000000);
         tu_cs_emit(cs, CP_EXEC_CS_1_NGROUPS_X(DIV_ROUND_UP(info->blocks[0],
                                                            local_size[0])));
         tu_cs_emit(cs, CP_EXEC_CS_2_NGROUPS_Y(DIV_ROUND_UP(info->blocks[1],
                                                            local_size[1])));
         tu_cs_emit(cs, CP_EXEC_CS_3_NGROUPS_Z(DIV_ROUND_UP(info->blocks[2],
                                                            local_size[2])));
      } else {
         tu_cs_emit_pkt7(cs, CP_EXEC_CS, 4);
         tu_cs_emit(cs, 0x00000000);
         tu_cs_emit(cs, CP_EXEC_CS_1_NGROUPS_X(info->blocks[0]));
         tu_cs_emit(cs, CP_EXEC_CS_2_NGROUPS_Y(info->blocks[1]));
         tu_cs_emit(cs, CP_EXEC_CS_3_NGROUPS_Z(info->blocks[2]));
      }

      trace_end_compute(&cmd->trace, cs);
   }

   /* For the workaround above, because it's using the "wrong" context for
    * SP_PS_INSTR_SIZE we should emit another dummy event write to avoid a
    * potential race between writing the register and the CP_EXEC_CS we just
    * did. We don't need to reset the register because it will be re-emitted
    * anyway when the next renderpass starts.
    */
   if (emit_instrlen_workaround) {
      tu_emit_event_write<CHIP>(cmd, cs, FD_LABEL);
   }

   cmd->state.total_dispatches++;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDispatchBase(VkCommandBuffer commandBuffer,
                   uint32_t base_x,
                   uint32_t base_y,
                   uint32_t base_z,
                   uint32_t x,
                   uint32_t y,
                   uint32_t z)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_dispatch_info info = {};

   info.blocks[0] = x;
   info.blocks[1] = y;
   info.blocks[2] = z;

   info.offsets[0] = base_x;
   info.offsets[1] = base_y;
   info.offsets[2] = base_z;
   tu_dispatch<CHIP>(cmd_buffer, &info);
}
TU_GENX(tu_CmdDispatchBase);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                       VkBuffer _buffer,
                       VkDeviceSize offset)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buffer, _buffer);
   struct tu_dispatch_info info = {};

   info.indirect = vk_buffer_address(&buffer->vk, offset);

   tu_dispatch<CHIP>(cmd_buffer, &info);
}
TU_GENX(tu_CmdDispatchIndirect);

void
tu_dispatch_unaligned(VkCommandBuffer commandBuffer,
                      uint32_t x, uint32_t y, uint32_t z)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_dispatch_info info = {};

   info.unaligned = true;
   info.blocks[0] = x;
   info.blocks[1] = y;
   info.blocks[2] = z;
   TU_CALLX(cmd_buffer->device, tu_dispatch)(cmd_buffer, &info);
}

void
tu_dispatch_unaligned_indirect(VkCommandBuffer commandBuffer,
                               VkDeviceAddress size_addr)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_dispatch_info info = {};

   info.unaligned = true;
   info.indirect = size_addr;

   TU_CALLX(cmd_buffer->device, tu_dispatch)(cmd_buffer, &info);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                     const VkSubpassEndInfo *pSubpassEndInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   if (TU_DEBUG(DYNAMIC)) {
      vk_common_CmdEndRenderPass2(commandBuffer, pSubpassEndInfo);
      return;
   }

   const VkRenderPassFragmentDensityMapOffsetEndInfoEXT *fdm_offset_info =
      vk_find_struct_const(pSubpassEndInfo->pNext,
                           RENDER_PASS_FRAGMENT_DENSITY_MAP_OFFSET_END_INFO_EXT);
   const VkOffset2D *fdm_offsets =
      (fdm_offset_info && fdm_offset_info->fragmentDensityOffsetCount > 0) ?
      fdm_offset_info->pFragmentDensityOffsets : NULL;

   VkOffset2D test_offsets[MAX_VIEWS];
   if (TU_DEBUG(FDM) && TU_DEBUG(FDM_OFFSET)) {
      for (unsigned i = 0; i < tu_fdm_num_layers(cmd_buffer); i++) {
         test_offsets[i] = { 64, 64 };
      }
      fdm_offsets = test_offsets;
   }

   TU_CALLX(cmd_buffer->device, tu_emit_custom_resolve_end)(cmd_buffer);

   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);
   TU_CALLX(cmd_buffer->device, tu_cmd_render)(cmd_buffer, fdm_offsets);

   cmd_buffer->state.cache.pending_flush_bits |=
      cmd_buffer->state.renderpass_cache.pending_flush_bits;
   tu_subpass_barrier(cmd_buffer, &cmd_buffer->state.pass->end_barrier, true);

   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer->state.attachments);

   tu_reset_render_pass(cmd_buffer);

   cmd_buffer->state.total_renderpasses++;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdEndRendering2EXT(VkCommandBuffer commandBuffer,
                       const VkRenderingEndInfoEXT *pRenderingEndInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   if (cmd_buffer->state.suspending) {
      cmd_buffer->state.suspended_pass.lrz = cmd_buffer->state.lrz;
      /* We cannot pass LRZ state to next resuming renderpass, so we have to
       * force disable it here.
       */
      TU_CALLX(cmd_buffer->device, tu_lrz_flush_valid_during_renderpass)
         (cmd_buffer, &cmd_buffer->draw_cs);
   } else {
      TU_CALLX(cmd_buffer->device, tu_emit_custom_resolve_end)(cmd_buffer);
   }

   const VkRenderPassFragmentDensityMapOffsetEndInfoEXT *fdm_offset_info =
      vk_find_struct_const(pRenderingEndInfo,
                           RENDER_PASS_FRAGMENT_DENSITY_MAP_OFFSET_END_INFO_EXT);
   const VkOffset2D *fdm_offsets =
      (fdm_offset_info && fdm_offset_info->fragmentDensityOffsetCount > 0) ?
      fdm_offset_info->pFragmentDensityOffsets : NULL;

   VkOffset2D test_offsets[MAX_VIEWS];
   if (TU_DEBUG(FDM) && TU_DEBUG(FDM_OFFSET)) {
      for (unsigned i = 0; i < tu_fdm_num_layers(cmd_buffer); i++) {
         test_offsets[i] = { 64, 64 };
      }
      fdm_offsets = test_offsets;
   }

   if (!cmd_buffer->state.suspending) {
      tu_cs_end(&cmd_buffer->draw_cs);
      tu_cs_end(&cmd_buffer->draw_epilogue_cs);

      if (cmd_buffer->state.suspend_resume == SR_IN_PRE_CHAIN) {
         tu_save_pre_chain(cmd_buffer);
         cmd_buffer->pre_chain.fdm_offset = !!fdm_offsets;
         if (fdm_offsets) {
            memcpy(cmd_buffer->pre_chain.fdm_offsets,
                   fdm_offsets, sizeof(VkOffset2D) *
                   tu_fdm_num_layers(cmd_buffer));
         }

         /* Even we don't call tu_cmd_render here, renderpass is finished
          * and draw states should be disabled.
          */
         tu_disable_draw_states(cmd_buffer, &cmd_buffer->cs);
      } else {
         TU_CALLX(cmd_buffer->device, tu_cmd_render)(cmd_buffer, fdm_offsets);
      }

      tu_reset_render_pass(cmd_buffer);
   }

   if (cmd_buffer->state.resuming && !cmd_buffer->state.suspending) {
      /* exiting suspend/resume chain */
      switch (cmd_buffer->state.suspend_resume) {
      case SR_IN_CHAIN:
         cmd_buffer->state.suspend_resume = SR_NONE;
         break;
      case SR_IN_PRE_CHAIN:
      case SR_IN_CHAIN_AFTER_PRE_CHAIN:
         cmd_buffer->state.suspend_resume = SR_AFTER_PRE_CHAIN;
         break;
      default:
         UNREACHABLE("suspending render pass not followed by resuming pass");
      }
   }

   if (!cmd_buffer->state.suspending) {
      cmd_buffer->state.total_renderpasses++;
   }
}

void
tu_barrier(struct tu_cmd_buffer *cmd,
           uint32_t dep_count,
           const VkDependencyInfo *dep_infos)
{
   VkPipelineStageFlags2 srcStage = 0;
   VkPipelineStageFlags2 dstStage = 0;
   BITMASK_ENUM(tu_cmd_access_mask) src_flags = 0;
   BITMASK_ENUM(tu_cmd_access_mask) dst_flags = 0;

   /* Inside a renderpass, we don't know yet whether we'll be using sysmem
    * so we have to use the sysmem flushes.
    */
   bool gmem = cmd->state.ccu_state == TU_CMD_CCU_GMEM &&
      !cmd->state.pass;

   for (uint32_t dep_idx = 0; dep_idx < dep_count; dep_idx++) {
      const VkDependencyInfo *dep_info = &dep_infos[dep_idx];

      for (uint32_t i = 0; i < dep_info->memoryBarrierCount; i++) {
         const VkMemoryBarrier2 *barrier = &dep_info->pMemoryBarriers[i];
         VkPipelineStageFlags2 sanitized_src_stage =
            sanitize_src_stage(barrier->srcStageMask);
         VkPipelineStageFlags2 sanitized_dst_stage =
            sanitize_dst_stage(barrier->dstStageMask);

         VkAccessFlags3KHR src_access_mask2 = 0, dst_access_mask2 = 0;
         const VkMemoryBarrierAccessFlags3KHR *access3 =
            vk_find_struct_const(barrier->pNext, MEMORY_BARRIER_ACCESS_FLAGS_3_KHR);
         if (access3) {
            src_access_mask2 = access3->srcAccessMask3;
            dst_access_mask2 = access3->dstAccessMask3;
         }

         src_flags |= vk2tu_access(barrier->srcAccessMask, src_access_mask2,
                                   sanitized_src_stage, false, gmem,
                                   cmd->device->vk.enabled_features.sparseResidencyAliased);
         dst_flags |= vk2tu_access(barrier->dstAccessMask, dst_access_mask2,
                                   sanitized_dst_stage, false, gmem,
                                   cmd->device->vk.enabled_features.sparseResidencyAliased);
         srcStage |= sanitized_src_stage;
         dstStage |= sanitized_dst_stage;
      }

      for (uint32_t i = 0; i < dep_info->bufferMemoryBarrierCount; i++) {
         const VkBufferMemoryBarrier2 *barrier =
            &dep_info->pBufferMemoryBarriers[i];
         VK_FROM_HANDLE(tu_buffer, buffer, barrier->buffer);
         bool sparse_aliasing =
            buffer->vk.create_flags & VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
         VkPipelineStageFlags2 sanitized_src_stage =
            sanitize_src_stage(barrier->srcStageMask);
         VkPipelineStageFlags2 sanitized_dst_stage =
            sanitize_dst_stage(barrier->dstStageMask);

         VkAccessFlags3KHR src_access_mask2 = 0, dst_access_mask2 = 0;
         const VkMemoryBarrierAccessFlags3KHR *access3 =
            vk_find_struct_const(barrier->pNext, MEMORY_BARRIER_ACCESS_FLAGS_3_KHR);
         if (access3) {
            src_access_mask2 = access3->srcAccessMask3;
            dst_access_mask2 = access3->dstAccessMask3;
         }

         src_flags |= vk2tu_access(barrier->srcAccessMask, src_access_mask2,
                                   sanitized_src_stage, false, gmem,
                                   sparse_aliasing);
         dst_flags |= vk2tu_access(barrier->dstAccessMask, dst_access_mask2,
                                   sanitized_dst_stage, false, gmem,
                                   sparse_aliasing);
         srcStage |= sanitized_src_stage;
         dstStage |= sanitized_dst_stage;
      }

      for (uint32_t i = 0; i < dep_info->imageMemoryBarrierCount; i++) {
         const VkImageMemoryBarrier2 *barrier =
            &dep_info->pImageMemoryBarriers[i];
         VK_FROM_HANDLE(tu_image, image, barrier->image);

         VkImageLayout old_layout = barrier->oldLayout;

         bool sparse_aliasing =
            image->vk.create_flags & VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
         if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
             old_layout == VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT) {
            /* The underlying memory for this image may have been used earlier
             * within the same queue submission for a different image, which
             * means that there may be old, stale cache entries which are in the
             * "wrong" location, which could cause problems later after writing
             * to the image. We don't want these entries being flushed later and
             * overwriting the actual image, so we need to flush the CCU.
             */
            if (vk_format_is_depth_or_stencil(image->vk.format)) {
               src_flags |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;
            } else {
               src_flags |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
            }
         }
         VkPipelineStageFlags2 sanitized_src_stage =
            sanitize_src_stage(barrier->srcStageMask);
         VkPipelineStageFlags2 sanitized_dst_stage =
            sanitize_dst_stage(barrier->dstStageMask);

         VkAccessFlags3KHR src_access_mask2 = 0, dst_access_mask2 = 0;
         const VkMemoryBarrierAccessFlags3KHR *access3 =
            vk_find_struct_const(barrier->pNext, MEMORY_BARRIER_ACCESS_FLAGS_3_KHR);
         if (access3) {
            src_access_mask2 = access3->srcAccessMask3;
            dst_access_mask2 = access3->dstAccessMask3;
         }

         src_flags |= vk2tu_access(barrier->srcAccessMask, src_access_mask2,
                                   sanitized_src_stage, true, gmem,
                                   sparse_aliasing);
         dst_flags |= vk2tu_access(barrier->dstAccessMask, dst_access_mask2,
                                   sanitized_dst_stage, true, gmem,
                                   sparse_aliasing);
         srcStage |= sanitized_src_stage;
         dstStage |= sanitized_dst_stage;
      }
   }

   if (cmd->state.pass) {
      const VkPipelineStageFlags framebuffer_space_stages =
         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

      /* We cannot have non-by-region "fb-space to fb-space" barriers.
       *
       * From the Vulkan 1.2.185 spec, section 7.6.1 "Subpass Self-dependency":
       *
       *    If the source and destination stage masks both include
       *    framebuffer-space stages, then dependencyFlags must include
       *    VK_DEPENDENCY_BY_REGION_BIT.
       *    [...]
       *    Each of the synchronization scopes and access scopes of a
       *    vkCmdPipelineBarrier2 or vkCmdPipelineBarrier command inside
       *    a render pass instance must be a subset of the scopes of one of
       *    the self-dependencies for the current subpass.
       *
       *    If the self-dependency has VK_DEPENDENCY_BY_REGION_BIT or
       *    VK_DEPENDENCY_VIEW_LOCAL_BIT set, then so must the pipeline barrier.
       *
       * By-region barriers are ok for gmem. All other barriers would involve
       * vtx stages which are NOT ok for gmem rendering.
       * See dep_invalid_for_gmem().
       */
      if ((srcStage & ~framebuffer_space_stages) ||
          (dstStage & ~framebuffer_space_stages)) {
         cmd->state.rp.disable_gmem = true;
         cmd->state.rp.gmem_disable_reason = "Non-framebuffer-space barrier";
      }
   }

   struct tu_cache_state *cache =
      cmd->state.pass  ? &cmd->state.renderpass_cache : &cmd->state.cache;

   /* a750 has a HW bug where writing a UBWC compressed image with a compute
    * shader followed by reading it as a texture (or readonly image) requires
    * a CACHE_CLEAN event. Some notes about this bug:
    * - It only happens after a blit happens.
    * - It's fast-clear related, it happens when the image is fast cleared
    *   before the write and the value read is (incorrectly) the fast clear
    *   color.
    * - CACHE_FLUSH is supposed to be the same as CACHE_CLEAN +
    *   CACHE_INVALIDATE, but it doesn't work whereas CACHE_CLEAN +
    *   CACHE_INVALIDATE does.
    *
    * The srcAccess can be replaced by a OpMemoryBarrier(MakeAvailable), so
    * we can't use that to insert the flush. Instead we use the shader source
    * stage.
    */
   if (cmd->device->physical_device->info->props.ubwc_coherency_quirk &&
       (srcStage &
        (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
         VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
         VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
         VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
         VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))) {
      cache->flush_bits |= TU_CMD_FLAG_CACHE_CLEAN;
      cache->pending_flush_bits &= ~TU_CMD_FLAG_CACHE_CLEAN;
   }

   tu_flush_for_access(cache, src_flags, dst_flags);

   enum tu_stage src_stage = vk2tu_src_stage(cmd->device, srcStage);
   enum tu_stage dst_stage = vk2tu_dst_stage(cmd->device, dstStage);
   tu_flush_for_stage(cache, src_stage, dst_stage);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                       const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   tu_barrier(cmd_buffer, 1, pDependencyInfo);
}

template <chip CHIP>
void
tu_write_event(struct tu_cmd_buffer *cmd, struct tu_event *event,
               VkPipelineStageFlags2 stageMask, unsigned value)
{
   struct tu_cs *cs = &cmd->cs;

   /* vkCmdSetEvent/vkCmdResetEvent cannot be called inside a render pass */
   assert(!cmd->state.pass);

   tu_emit_cache_flush<CHIP>(cmd);

   /* Flags that only require a top-of-pipe event. DrawIndirect parameters are
    * read by the CP, so the draw indirect stage counts as top-of-pipe too.
    */
   VkPipelineStageFlags2 top_of_pipe_flags =
      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

   if (!(stageMask & ~top_of_pipe_flags)) {
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
      tu_cs_emit_qw(cs, event->bo.iova); /* ADDR_LO/HI */
      tu_cs_emit(cs, value);
   } else {
      /* Use a RB_DONE_TS event to wait for everything to complete. */
      if (CHIP == A6XX) {
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
         tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS));
      } else {
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
         tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = RB_DONE_TS,
                                          .write_src = EV_WRITE_USER_32B,
                                          .write_dst = EV_DST_RAM,
                                          .write_enabled = true).value);
      }

      tu_cs_emit_qw(cs, event->bo.iova);
      tu_cs_emit(cs, value);
   }
}
TU_GENX(tu_write_event);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer,
                                   const VkConditionalRenderingBeginInfoEXT *pConditionalRenderingBegin)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.predication_active = true;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   /* Wait for any writes to the predicate to land */
   if (cmd->state.pass)
      tu_emit_cache_flush_renderpass<CHIP>(cmd);
   else
      tu_emit_cache_flush<CHIP>(cmd);

   VK_FROM_HANDLE(tu_buffer, buf, pConditionalRenderingBegin->buffer);
   uint64_t iova = vk_buffer_address(&buf->vk, pConditionalRenderingBegin->offset);

   /* qcom doesn't support 32-bit reference values, only 64-bit, but Vulkan
    * mandates 32-bit comparisons. Our workaround is to copy the the reference
    * value to the low 32-bits of a location where the high 32 bits are known
    * to be 0 and then compare that.
    *
    * BR and BV use separate predicate values so that setting the predicate
    * doesn't have to be synchronized between them.
    */
   if (CHIP >= A7XX) {
      if (!cmd->state.pass) {
         tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
         tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BOTH));
      }
      tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) |
                             CP_COND_REG_EXEC_0_BR);
   }

   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
   tu_cs_emit(cs, 0);
   tu_cs_emit_qw(cs, global_iova(cmd, predicate));
   tu_cs_emit_qw(cs, iova);

   if (CHIP >= A7XX) {
      tu_cond_exec_end(cs);
      tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) |
                             CP_COND_REG_EXEC_0_BV);
      tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
      tu_cs_emit(cs, 0);
      tu_cs_emit_qw(cs, global_iova(cmd, bv_predicate));
      tu_cs_emit_qw(cs, iova);
      tu_cond_exec_end(cs);
   }

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 1);

   bool inv = pConditionalRenderingBegin->flags & VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;

   if (CHIP >= A7XX) {
      tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) |
                             CP_COND_REG_EXEC_0_BR);
   }
   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_SET, 3);
   tu_cs_emit(cs, CP_DRAW_PRED_SET_0_SRC(PRED_SRC_MEM) |
                  CP_DRAW_PRED_SET_0_TEST(inv ? EQ_0_PASS : NE_0_PASS));
   tu_cs_emit_qw(cs, global_iova(cmd, predicate));

   if (CHIP >= A7XX) {
      tu_cond_exec_end(cs);
      tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) |
                             CP_COND_REG_EXEC_0_BV);
      tu_cs_emit_pkt7(cs, CP_DRAW_PRED_SET, 3);
      tu_cs_emit(cs, CP_DRAW_PRED_SET_0_SRC(PRED_SRC_MEM) |
                     CP_DRAW_PRED_SET_0_TEST(inv ? EQ_0_PASS : NE_0_PASS));
      tu_cs_emit_qw(cs, global_iova(cmd, bv_predicate));
      tu_cond_exec_end(cs);
   }

   /* Restore original BR thread after setting BOTH */
   if (CHIP >= A7XX && !cmd->state.pass) {
      tu7_set_thread_br_patchpoint(cmd, cs, false);
   }
}
TU_GENX(tu_CmdBeginConditionalRenderingEXT);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.predication_active = false;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   if (CHIP >= A7XX && !cmd->state.pass) {
      tu_cs_emit_pkt7(cs, CP_THREAD_CONTROL, 1);
      tu_cs_emit(cs, CP_THREAD_CONTROL_0_THREAD(CP_SET_THREAD_BOTH));
   }

   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0);

   if (CHIP >= A7XX && !cmd->state.pass) {
      tu7_set_thread_br_patchpoint(cmd, cs, false);
   }
}
TU_GENX(tu_CmdEndConditionalRenderingEXT);

template <chip CHIP>
void
tu_CmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer,
                            VkPipelineStageFlagBits2 pipelineStage,
                            VkBuffer dstBuffer,
                            VkDeviceSize dstOffset,
                            uint32_t marker)
{
   /* Almost the same as tu_write_event, but also allowed in renderpass */
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_buffer, buffer, dstBuffer);

   uint64_t va = vk_buffer_address(&buffer->vk, dstOffset);

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;
   struct tu_cache_state *cache =
      cmd->state.pass ? &cmd->state.renderpass_cache : &cmd->state.cache;

   /* From the Vulkan 1.2.203 spec:
    *
    *    The access scope for buffer marker writes falls under
    *    the VK_ACCESS_TRANSFER_WRITE_BIT, and the pipeline stages for
    *    identifying the synchronization scope must include both pipelineStage
    *    and VK_PIPELINE_STAGE_TRANSFER_BIT.
    *
    * Transfer operations use CCU however here we write via CP.
    * Flush CCU in order to make the results of previous transfer
    * operation visible to CP.
    */
   tu_flush_for_access(cache, TU_ACCESS_NONE, TU_ACCESS_SYSMEM_WRITE);

   /* Flags that only require a top-of-pipe event. DrawIndirect parameters are
    * read by the CP, so the draw indirect stage counts as top-of-pipe too.
    */
   VkPipelineStageFlags2 top_of_pipe_flags =
      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

   bool is_top_of_pipe = !(pipelineStage & ~top_of_pipe_flags);

   /* We have to WFI only if we flushed CCU here and are using CP_MEM_WRITE.
    * Otherwise:
    * - We do CP_EVENT_WRITE(RB_DONE_TS) which should wait for flushes;
    * - There was a barrier to synchronize other writes with WriteBufferMarkerAMD
    *   and they had to include our pipelineStage which forces the WFI.
    */
   if (cache->flush_bits && is_top_of_pipe) {
      cache->flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
   }

   if (cmd->state.pass) {
      tu_emit_cache_flush_renderpass<CHIP>(cmd);
   } else {
      tu_emit_cache_flush<CHIP>(cmd);
   }

   if (is_top_of_pipe) {
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
      tu_cs_emit_qw(cs, va); /* ADDR_LO/HI */
      tu_cs_emit(cs, marker);
   } else {
      /* Use a RB_DONE_TS event to wait for everything to complete. */
      if (CHIP == A6XX) {
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
         tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS));
      } else {
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
         tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = RB_DONE_TS,
                                          .write_src = EV_WRITE_USER_32B,
                                          .write_dst = EV_DST_RAM,
                                          .write_enabled = true).value);
      }
      tu_cs_emit_qw(cs, va);
      tu_cs_emit(cs, marker);
   }

   /* Make sure the result of this write is visible to others. */
   tu_flush_for_access(cache, TU_ACCESS_CP_WRITE, TU_ACCESS_NONE);
}
TU_GENX(tu_CmdWriteBufferMarker2AMD);

void
tu_write_buffer_cp(VkCommandBuffer commandBuffer,
                   VkDeviceAddress addr,
                   void *data, uint32_t size)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   TU_CALLX(cmd->device, tu_emit_cache_flush)(cmd);

   struct tu_cs *cs = &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 2 + size / 4);
   tu_cs_emit_qw(cs, addr);
   tu_cs_emit_array(cs, (uint32_t *)data, size / 4);
}

void
tu_flush_buffer_write_cp(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   struct tu_cache_state *cache = &cmd->state.cache;
   tu_flush_for_access(cache, TU_ACCESS_CP_WRITE, (enum tu_cmd_access_mask)0);
}
