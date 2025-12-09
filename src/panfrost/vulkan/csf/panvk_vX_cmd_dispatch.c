/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_meta.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instr.h"
#include "panvk_macros.h"
#include "panvk_meta.h"
#include "panvk_physical_device.h"
#include "panvk_tracepoints.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_props.h"

#include <vulkan/vulkan_core.h>

static VkResult
prepare_driver_set(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_shader_desc_state *cs_desc_state =
      &cmdbuf->state.compute.cs.desc;

   if (!compute_state_dirty(cmdbuf, CS) &&
       !compute_state_dirty(cmdbuf, DESC_STATE))
      return VK_SUCCESS;

   const struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.compute.desc_state;
   const struct panvk_shader_variant *cs =
      panvk_shader_only_variant(cmdbuf->state.compute.shader);
   uint32_t desc_count = cs->desc_info.dyn_bufs.count + 1;
   struct pan_ptr driver_set = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, desc_count * PANVK_DESCRIPTOR_SIZE, PANVK_DESCRIPTOR_SIZE);
   struct panvk_opaque_desc *descs = driver_set.cpu;

   if (!driver_set.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   /* Dummy sampler always comes first. */
   pan_cast_and_pack(&descs[0], SAMPLER, cfg) {
      cfg.clamp_integer_array_indices = false;
   }

   panvk_per_arch(cmd_fill_dyn_bufs)(desc_state, cs,
                                     (struct mali_buffer_packed *)(&descs[1]));

   cs_desc_state->driver_set.dev_addr = driver_set.gpu;
   cs_desc_state->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   return VK_SUCCESS;
}

uint64_t
panvk_per_arch(cmd_dispatch_prepare_tls)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *cs,
   const struct pan_compute_dim *dim, bool indirect)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);

   struct pan_ptr tsd = panvk_cmd_alloc_desc(cmdbuf, LOCAL_STORAGE);
   if (!tsd.gpu)
      return tsd.gpu;

   struct pan_tls_info tlsinfo = {
      .tls.size = cs->info.tls_size,
      .wls.size = cs->info.wls_size,
   };

   if (tlsinfo.wls.size) {
      unsigned core_id_range;
      pan_query_core_count(&phys_dev->kmod.props, &core_id_range);

      tlsinfo.wls.instances = pan_calc_wls_instances(
         &cs->cs.local_size, &phys_dev->kmod.props, indirect ? NULL : dim);

      unsigned wls_total_size = pan_calc_total_wls_size(
         tlsinfo.wls.size, tlsinfo.wls.instances, core_id_range);

      /* TODO: Reuse WLS allocation for all dispatch commands in the command
       * buffer, similar to what we do for TLS in draw. As WLS size (and
       * instance count) might differ significantly between dispatch commands,
       * rather than track a single maximum size, we might want to consider
       * multiple allocations for different size buckets. */
      tlsinfo.wls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, wls_total_size, 4096).gpu;
      if (!tlsinfo.wls.ptr)
         return 0;
   }

   cmdbuf->state.tls.info.tls.size =
      MAX2(cs->info.tls_size, cmdbuf->state.tls.info.tls.size);

   if (!cmdbuf->state.tls.desc.gpu) {
      cmdbuf->state.tls.desc = panvk_cmd_alloc_desc(cmdbuf, LOCAL_STORAGE);
      if (!cmdbuf->state.tls.desc.gpu)
         return 0;
   }

   GENX(pan_emit_tls)(&tlsinfo, tsd.cpu);

   return tsd.gpu;
}

static void
cmd_dispatch(struct panvk_cmd_buffer *cmdbuf, struct panvk_dispatch_info *info)
{
   const struct panvk_shader_variant *cs =
      panvk_shader_only_variant(cmdbuf->state.compute.shader);
   VkResult result;

   /* If there's no compute shader, we can skip the dispatch. */
   if (!panvk_priv_mem_dev_addr(cs->spd))
      return;

   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.compute.desc_state;
   struct panvk_shader_desc_state *cs_desc_state =
      &cmdbuf->state.compute.cs.desc;
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].tracing;

   struct pan_compute_dim dim = {
      info->direct.wg_count.x,
      info->direct.wg_count.y,
      info->direct.wg_count.z,
   };
   bool indirect = info->indirect.buffer_dev_addr != 0;

   uint64_t tsd =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, cs, &dim, indirect);
   if (!tsd)
      return;

   /* Only used for indirect dispatch */
   unsigned wg_per_task = 0;
   if (indirect)
      wg_per_task = pan_calc_workgroups_per_task(&cs->cs.local_size,
                                                 &phys_dev->kmod.props);

   if (compute_state_dirty(cmdbuf, DESC_STATE) ||
       compute_state_dirty(cmdbuf, CS)) {
      result = panvk_per_arch(cmd_prepare_push_descs)(
         cmdbuf, desc_state, cs->desc_info.used_set_mask);
      if (result != VK_SUCCESS)
         return;
   }

   panvk_per_arch(cmd_prepare_dispatch_sysvals)(cmdbuf, info);

   result = prepare_driver_set(cmdbuf);
   if (result != VK_SUCCESS)
      return;

   result = panvk_per_arch(cmd_prepare_push_uniforms)(cmdbuf, cs, 1);
   if (result != VK_SUCCESS)
      return;

   if (compute_state_dirty(cmdbuf, CS) ||
       compute_state_dirty(cmdbuf, DESC_STATE)) {
      result = panvk_per_arch(cmd_prepare_shader_res_table)(
         cmdbuf, desc_state, cs, cs_desc_state, 1);
      if (result != VK_SUCCESS)
         return;
   }

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);

   /* Copy the global TLS pointer to the per-job TSD. */
   if (cs->info.tls_size) {
      cs_move64_to(b, cs_scratch_reg64(b, 0), cmdbuf->state.tls.desc.gpu);
      cs_load64_to(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_move64_to(b, cs_scratch_reg64(b, 0), tsd);
      cs_store64(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_flush_stores(b);
   }

   cs_update_compute_ctx(b) {
      if (compute_state_dirty(cmdbuf, CS) ||
          compute_state_dirty(cmdbuf, DESC_STATE))
         cs_move64_to(b, cs_sr_reg64(b, COMPUTE, SRT_0),
                      cs_desc_state->res_table);

      if (compute_state_dirty(cmdbuf, PUSH_UNIFORMS)) {
         uint64_t fau_ptr = cmdbuf->state.compute.push_uniforms |
                            ((uint64_t)cs->fau.total_count << 56);
         cs_move64_to(b, cs_sr_reg64(b, COMPUTE, FAU_0), fau_ptr);
      }

      if (compute_state_dirty(cmdbuf, CS))
         cs_move64_to(b, cs_sr_reg64(b, COMPUTE, SPD_0),
                      panvk_priv_mem_dev_addr(cs->spd));

      cs_move64_to(b, cs_sr_reg64(b, COMPUTE, TSD_0), tsd);

      /* Global attribute offset */
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, GLOBAL_ATTRIBUTE_OFFSET),
                   0);

      struct mali_compute_size_workgroup_packed wg_size;
      pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
         cfg.workgroup_size_x = cs->cs.local_size.x;
         cfg.workgroup_size_y = cs->cs.local_size.y;
         cfg.workgroup_size_z = cs->cs.local_size.z;
         cfg.allow_merging_workgroups = false;
      }
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, WG_SIZE),
                   wg_size.opaque[0]);

      /* global_id and wg_id in NIR are expected to have base_workgroup_id added.
       * Because job offset doesn't apply to wg_id on Mali, we set this to 0.
       * XXX: We could teach nir_lower_system_values how to handle Mali weird
       * case. */
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_X), 0);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_Y), 0);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_Z), 0);

      if (indirect) {
         /* Load parameters from indirect buffer and update workgroup count
          * registers and sysvals */
         cs_move64_to(b, cs_scratch_reg64(b, 0),
                      info->indirect.buffer_dev_addr);
         cs_load_to(b, cs_sr_reg_tuple(b, COMPUTE, JOB_SIZE_X, 3),
                    cs_scratch_reg64(b, 0), BITFIELD_MASK(3), 0);
         cs_move64_to(b, cs_scratch_reg64(b, 0),
                      cmdbuf->state.compute.push_uniforms);

         if (shader_uses_sysval(cs, compute, num_work_groups.x)) {
            cs_store32(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X),
                       cs_scratch_reg64(b, 0),
                       shader_remapped_sysval_offset(
                          cs, sysval_offset(compute, num_work_groups.x)));
         }

         if (shader_uses_sysval(cs, compute, num_work_groups.y)) {
            cs_store32(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Y),
                       cs_scratch_reg64(b, 0),
                       shader_remapped_sysval_offset(
                          cs, sysval_offset(compute, num_work_groups.y)));
         }

         if (shader_uses_sysval(cs, compute, num_work_groups.z)) {
            cs_store32(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Z),
                       cs_scratch_reg64(b, 0),
                       shader_remapped_sysval_offset(
                          cs, sysval_offset(compute, num_work_groups.z)));
         }

         cs_flush_stores(b);
      } else {
         cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X),
                      info->direct.wg_count.x);
         cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Y),
                      info->direct.wg_count.y);
         cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Z),
                      info->direct.wg_count.z);
      }
   }

   struct cs_index next_iter_sb_scratch = cs_scratch_reg_tuple(b, 0, 2);
   panvk_per_arch(cs_next_iter_sb)(cmdbuf, PANVK_SUBQUEUE_COMPUTE,
                                   next_iter_sb_scratch);

   if (indirect) {
      /* Use run_compute with a set task axis instead of run_compute_indirect as
       * run_compute_indirect has been found to cause intermittent hangs. This
       * is safe, as the task increment will be clamped by the job size along
       * the specified axis.
       * The chosen task axis is potentially suboptimal, as choosing good
       * increment/axis parameters requires knowledge of job dimensions, but
       * this is somewhat offset by run_compute being a native instruction. */
      unsigned task_axis = MALI_TASK_AXIS_X;
      cs_trace_run_compute(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                           wg_per_task, task_axis,
                           cs_shader_res_sel(0, 0, 0, 0));
   } else {
      unsigned task_axis = MALI_TASK_AXIS_X;
      unsigned task_increment = 0;
      panvk_per_arch(calculate_task_axis_and_increment)(
         cs, phys_dev, &task_axis, &task_increment);
      cs_trace_run_compute(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                           task_increment, task_axis,
                           cs_shader_res_sel(0, 0, 0, 0));
   }

#if PAN_ARCH >= 11
   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index add_val = cs_scratch_reg64(b, 2);

   cs_load64_to(b, sync_addr, cs_subqueue_ctx_reg(b),
                offsetof(struct panvk_cs_subqueue_context, syncobjs));

   cs_add64(b, sync_addr, sync_addr,
            PANVK_SUBQUEUE_COMPUTE * sizeof(struct panvk_cs_sync64));
   cs_move64_to(b, add_val, 1);
   panvk_instr_sync64_add(cmdbuf, PANVK_SUBQUEUE_COMPUTE, true,
                          MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr,
                          cs_defer_indirect());
#else
   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index iter_sb = cs_scratch_reg32(b, 2);
   struct cs_index cmp_scratch = cs_scratch_reg32(b, 3);
   struct cs_index add_val = cs_scratch_reg64(b, 4);

   cs_load_to(b, cs_scratch_reg_tuple(b, 0, 3), cs_subqueue_ctx_reg(b),
              BITFIELD_MASK(3),
              offsetof(struct panvk_cs_subqueue_context, syncobjs));

   cs_add64(b, sync_addr, sync_addr,
            PANVK_SUBQUEUE_COMPUTE * sizeof(struct panvk_cs_sync64));
   cs_move64_to(b, add_val, 1);

   cs_match_iter_sb(b, x, iter_sb, cmp_scratch) {
      panvk_instr_sync64_add(cmdbuf, PANVK_SUBQUEUE_COMPUTE, true,
                             MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr,
                             cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC)));
   }
#endif

   ++cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].relative_sync_point;
   clear_dirty_after_dispatch(cmdbuf);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDispatchBase)(VkCommandBuffer commandBuffer,
                                uint32_t baseGroupX, uint32_t baseGroupY,
                                uint32_t baseGroupZ, uint32_t groupCountX,
                                uint32_t groupCountY, uint32_t groupCountZ)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   const struct panvk_shader_variant *shader =
      panvk_shader_only_variant(cmdbuf->state.compute.shader);
   struct panvk_dispatch_info info = {
      .wg_base = {baseGroupX, baseGroupY, baseGroupZ},
      .direct.wg_count = {groupCountX, groupCountY, groupCountZ},
   };

   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_COMPUTE, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_DISPATCH);

   cmd_dispatch(cmdbuf, &info);

   struct panvk_instr_end_args instr_info = {
      .dispatch = {
         .base_group_x = baseGroupX,
         .base_group_y = baseGroupY,
         .base_group_z = baseGroupZ,
         .group_count_x = groupCountX,
         .group_count_y = groupCountY,
         .group_count_z = groupCountZ,
         .group_size_x = shader->cs.local_size.x,
         .group_size_y = shader->cs.local_size.y,
         .group_size_z = shader->cs.local_size.z,
      }};
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_COMPUTE, cmdbuf, PANVK_INSTR_WORK_TYPE_DISPATCH,
      &instr_info, cs_defer(dev->csf.sb.all_iters_mask, 0));
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDispatchIndirect)(VkCommandBuffer commandBuffer,
                                    VkBuffer _buffer, VkDeviceSize offset)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, _buffer);
   uint64_t buffer_gpu = panvk_buffer_gpu_ptr(buffer, offset);
   struct panvk_dispatch_info info = {
      .indirect.buffer_dev_addr = buffer_gpu,
   };

   panvk_per_arch(panvk_instr_begin_work)(
      PANVK_SUBQUEUE_COMPUTE, cmdbuf, PANVK_INSTR_WORK_TYPE_DISPATCH_INDIRECT);

   cmd_dispatch(cmdbuf, &info);

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_instr_end_args instr_info = {.dispatch_indirect = {
                                                .buffer_gpu = buffer_gpu,
                                             }};
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_COMPUTE, cmdbuf, PANVK_INSTR_WORK_TYPE_DISPATCH_INDIRECT,
      &instr_info, cs_defer(dev->csf.sb.all_iters_mask, 0));
}
