/*
 * Copyright © 2024 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_precomp.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_meta.h"
#include "panvk_physical_device.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_jc.h"
#include "pan_props.h"

#include <vulkan/vulkan_core.h>

uint64_t
panvk_per_arch(cmd_dispatch_prepare_tls)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader_variant *cs,
   const struct pan_compute_dim *dim, bool indirect)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   assert(batch);

   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);

   panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, false);

   batch->tlsinfo.tls.size = cs->info.tls_size;
   batch->tlsinfo.wls.size = cs->info.wls_size;

   if (batch->tlsinfo.wls.size) {
      unsigned core_id_range;

      pan_query_core_count(&phys_dev->kmod.props, &core_id_range);
      batch->tlsinfo.wls.instances = pan_calc_wls_instances(
         &cs->cs.local_size, &phys_dev->kmod.props, indirect ? NULL : dim);
      batch->wls_total_size = pan_calc_total_wls_size(
         batch->tlsinfo.wls.size, batch->tlsinfo.wls.instances, core_id_range);
   }

   return batch->tls.gpu;
}

static void
cmd_dispatch(struct panvk_cmd_buffer *cmdbuf, struct panvk_dispatch_info *info)
{
   const struct panvk_shader_variant *cs =
      panvk_shader_only_variant(cmdbuf->state.compute.shader);
   VkResult result;

   /* If there's no compute shader, we can skip the dispatch. */
   if (!panvk_priv_mem_dev_addr(cs->rsd))
      return;

   panvk_per_arch(cmd_close_batch)(cmdbuf);
   struct panvk_batch *batch = panvk_per_arch(cmd_open_batch)(cmdbuf);

   struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.compute.desc_state;
   struct panvk_shader_desc_state *cs_desc_state =
      &cmdbuf->state.compute.cs.desc;

   struct pan_compute_dim wg_count = {
      info->direct.wg_count.x,
      info->direct.wg_count.y,
      info->direct.wg_count.z,
   };
   bool indirect = info->indirect.buffer_dev_addr != 0;
   uint64_t tsd = panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, cs,
                                                           &wg_count, indirect);

   result = panvk_per_arch(cmd_prepare_push_descs)(
      cmdbuf, desc_state, cs->desc_info.used_set_mask);
   if (result != VK_SUCCESS)
      return;

   if (compute_state_dirty(cmdbuf, CS) ||
       compute_state_dirty(cmdbuf, DESC_STATE)) {
      result = panvk_per_arch(cmd_prepare_dyn_ssbos)(cmdbuf, desc_state, cs,
                                                     cs_desc_state);
      if (result != VK_SUCCESS)
         return;
   }

   panvk_per_arch(cmd_prepare_dispatch_sysvals)(cmdbuf, info);

   result = panvk_per_arch(cmd_prepare_push_uniforms)(cmdbuf, cs, 1);
   if (result != VK_SUCCESS)
      return;

   struct pan_ptr copy_desc_job = {0};

   if (compute_state_dirty(cmdbuf, CS) ||
       compute_state_dirty(cmdbuf, DESC_STATE)) {
      result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
         cmdbuf, desc_state, cs, cs_desc_state);

      result = panvk_per_arch(meta_get_copy_desc_job)(
         cmdbuf, cs, &cmdbuf->state.compute.desc_state, cs_desc_state, 0,
         &copy_desc_job);
      if (result != VK_SUCCESS)
         return;

      if (copy_desc_job.cpu)
         util_dynarray_append(&batch->jobs, copy_desc_job.cpu);
   }

   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!job.gpu)
      return;

   util_dynarray_append(&batch->jobs, job.cpu);

   if (!indirect) {
      pan_pack_work_groups_compute(
         pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), wg_count.x,
         wg_count.y, wg_count.z, cs->cs.local_size.x,
         cs->cs.local_size.y, cs->cs.local_size.z, false, false);
   }

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(cs->cs.local_size.x + 1) +
                           util_logbase2_ceil(cs->cs.local_size.y + 1) +
                           util_logbase2_ceil(cs->cs.local_size.z + 1);
   }

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(cs->rsd);
      cfg.attributes = cs_desc_state->img_attrib_table;
      cfg.attribute_buffers =
         cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.thread_storage = tsd;
      cfg.uniform_buffers = cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = cmdbuf->state.compute.push_uniforms;
      cfg.textures = cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
   }

   unsigned indirect_dep = 0;
   if (indirect) {
      /* We redirect write to memory sink for null pointers */
      uint64_t num_work_groups_x_sysval_addr = 0x8ull << 60;
      uint64_t num_work_groups_y_sysval_addr = 0x8ull << 60;
      uint64_t num_work_groups_z_sysval_addr = 0x8ull << 60;

      if (shader_uses_sysval(cs, compute, num_work_groups.x)) {
         num_work_groups_x_sysval_addr =
            cmdbuf->state.compute.push_uniforms +
            shader_remapped_sysval_offset(
               cs, sysval_offset(compute, num_work_groups.x));
      }

      if (shader_uses_sysval(cs, compute, num_work_groups.y)) {
         num_work_groups_y_sysval_addr =
            cmdbuf->state.compute.push_uniforms +
            shader_remapped_sysval_offset(
               cs, sysval_offset(compute, num_work_groups.y));
      }

      if (shader_uses_sysval(cs, compute, num_work_groups.z)) {
         num_work_groups_z_sysval_addr =
            cmdbuf->state.compute.push_uniforms +
            shader_remapped_sysval_offset(
               cs, sysval_offset(compute, num_work_groups.z));
      }

      struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
      enum panlib_barrier precomp_barrier =
         copy_desc_job.gpu == 0 ? PANLIB_BARRIER_JM_SUPPRESS_PREFETCH
                                : PANLIB_BARRIER_NONE;

      panlib_indirect_dispatch(
         &precomp_ctx, panlib_1d(1), precomp_barrier,
         info->indirect.buffer_dev_addr, cs->cs.local_size.x,
         cs->cs.local_size.y, cs->cs.local_size.z, job.gpu,
         num_work_groups_x_sysval_addr, num_work_groups_y_sysval_addr,
         num_work_groups_z_sysval_addr);
      indirect_dep = batch->vtc_jc.job_index;
   }

   util_dynarray_append(&batch->jobs, job.cpu);

   unsigned copy_desc_dep =
      copy_desc_job.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false,
                          indirect, 0, indirect_dep, &copy_desc_job, false)
         : indirect_dep;

   pan_jc_add_job(&batch->vtc_jc,
                  indirect ? MALI_JOB_TYPE_NOT_STARTED : MALI_JOB_TYPE_COMPUTE,
                  false, false, 0, copy_desc_dep, &job, false);

   panvk_per_arch(cmd_close_batch)(cmdbuf);
   clear_dirty_after_dispatch(cmdbuf);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDispatchBase)(VkCommandBuffer commandBuffer,
                                uint32_t baseGroupX, uint32_t baseGroupY,
                                uint32_t baseGroupZ, uint32_t groupCountX,
                                uint32_t groupCountY, uint32_t groupCountZ)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
      return;

   struct panvk_dispatch_info info = {
      .wg_base = {baseGroupX, baseGroupY, baseGroupZ},
      .direct.wg_count = {groupCountX, groupCountY, groupCountZ},
   };
   cmd_dispatch(cmdbuf, &info);
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
   cmd_dispatch(cmdbuf, &info);
}
