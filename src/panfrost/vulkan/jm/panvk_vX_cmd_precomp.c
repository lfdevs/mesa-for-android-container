/*
 * Copyright © 2024 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bifrost/bifrost_compile.h"
#include "pan_desc.h"
#include "pan_encoder.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_precomp.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_precomp_cache.h"

void
panvk_per_arch(dispatch_precomp)(struct panvk_precomp_ctx *ctx,
                                 struct panlib_precomp_grid grid,
                                 enum panlib_barrier barrier,
                                 enum libpan_shaders_program idx, void *data,
                                 size_t data_size)
{
   enum panlib_barrier supported_barriers =
      PANLIB_BARRIER_JM_BARRIER | PANLIB_BARRIER_JM_SUPPRESS_PREFETCH;
   assert(!(barrier & ~supported_barriers) && "Unsupported barrier flags");

   struct panvk_cmd_buffer *cmdbuf = ctx->cmdbuf;

   /* Make sure we have a batch opened to queue our COMPUTE job to. */
   if (!cmdbuf->cur_batch)
      panvk_per_arch(cmd_open_batch)(cmdbuf);

   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct panvk_shader_variant *shader =
      panvk_per_arch(precomp_cache_get)(dev->precomp_cache, idx);

   assert(shader);
   assert(batch && "Need current batch to be present!");

   struct pan_ptr push_uniforms = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size, 16);

   assert(push_uniforms.gpu);

   struct bifrost_precompiled_kernel_sysvals sysvals;
   sysvals.num_workgroups.x = grid.count[0];
   sysvals.num_workgroups.y = grid.count[1];
   sysvals.num_workgroups.z = grid.count[2];
   sysvals.printf_buffer_address = dev->printf.bo->addr.dev;

   bifrost_precompiled_kernel_prepare_push_uniforms(push_uniforms.cpu, data,
                                                    data_size, &sysvals);

   struct pan_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   assert(job.gpu);

   pan_pack_work_groups_compute(
      pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), grid.count[0],
      grid.count[1], grid.count[2], shader->cs.local_size.x,
      shader->cs.local_size.y, shader->cs.local_size.z, false, false);

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(shader->cs.local_size.x + 1) +
                           util_logbase2_ceil(shader->cs.local_size.y + 1) +
                           util_logbase2_ceil(shader->cs.local_size.z + 1);
   }

   struct pan_compute_dim dim = {.x = grid.count[0],
                                 .y = grid.count[1],
                                 .z = grid.count[2]};
   uint64_t tld =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, shader, &dim, false);
   assert(tld);

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(shader->rsd),
      cfg.push_uniforms = push_uniforms.gpu;
      cfg.thread_storage = tld;
   }

   util_dynarray_append(&batch->jobs, job.cpu);

   bool job_barrier = (barrier & PANLIB_BARRIER_JM_BARRIER) != 0;
   bool suppress_prefetch =
      (barrier & PANLIB_BARRIER_JM_SUPPRESS_PREFETCH) != 0;

   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, job_barrier,
                  suppress_prefetch, grid.jm.local_dep, grid.jm.global_dep,
                  &job, false);
}
