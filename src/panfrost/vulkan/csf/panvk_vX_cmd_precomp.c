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
#include "panvk_instr.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_precomp_cache.h"
#include "panvk_queue.h"

void
panvk_per_arch(dispatch_precomp)(struct panvk_precomp_ctx *ctx,
                                 struct panlib_precomp_grid grid,
                                 enum panlib_barrier barrier,
                                 enum libpan_shaders_program idx, void *data,
                                 size_t data_size)
{
   enum panlib_barrier supported_barriers =
      PANLIB_BARRIER_CSF_SYNC | PANLIB_BARRIER_CSF_WAIT;
   assert(!(barrier & ~supported_barriers) && "Unsupported barrier flags");
   assert(!(barrier & PANLIB_BARRIER_CSF_SYNC &&
            barrier & PANLIB_BARRIER_CSF_WAIT) &&
          "Cannot use both CSF_SYNC and CSF_WAIT barriers simultaneously");


   struct panvk_cmd_buffer *cmdbuf = ctx->cmdbuf;
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   const struct panvk_shader_variant *shader =
      panvk_per_arch(precomp_cache_get)(dev->precomp_cache, idx);
   assert(shader);

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

   struct pan_compute_dim dim = {.x = grid.count[0],
                                 .y = grid.count[1],
                                 .z = grid.count[2]};

   uint64_t tsd =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, shader, &dim, false);
   assert(tsd);

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].tracing;

   /* Copy the global TLS pointer to the per-job TSD. */
   if (shader->info.tls_size) {
      cs_move64_to(b, cs_scratch_reg64(b, 0), cmdbuf->state.tls.desc.gpu);
      cs_load64_to(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_move64_to(b, cs_scratch_reg64(b, 0), tsd);
      cs_store64(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_flush_stores(b);
   }

   cs_update_compute_ctx(b) {
      /* No resource table */
      cs_move64_to(b, cs_sr_reg64(b, COMPUTE, SRT_0), 0);

      uint64_t fau_count =
         DIV_ROUND_UP(BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size, 8);
      uint64_t fau_ptr = push_uniforms.gpu | (fau_count << 56);
      cs_move64_to(b, cs_sr_reg64(b, COMPUTE, FAU_0), fau_ptr);

      cs_move64_to(b, cs_sr_reg64(b, COMPUTE, SPD_0),
                   panvk_priv_mem_dev_addr(shader->spd));

      cs_move64_to(b, cs_sr_reg64(b, COMPUTE, TSD_0), tsd);

      /* Global attribute offset */
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, GLOBAL_ATTRIBUTE_OFFSET), 0);

      struct mali_compute_size_workgroup_packed wg_size;
      pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
         cfg.workgroup_size_x = shader->cs.local_size.x;
         cfg.workgroup_size_y = shader->cs.local_size.y;
         cfg.workgroup_size_z = shader->cs.local_size.z;
         cfg.allow_merging_workgroups = false;
      }
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, WG_SIZE), wg_size.opaque[0]);

      /* Job offset */
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_X), 0);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_Y), 0);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_OFFSET_Z), 0);

      /* Job size */
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X), grid.count[0]);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Y), grid.count[1]);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Z), grid.count[2]);
   }

   struct cs_index next_iter_sb_scratch = cs_scratch_reg_tuple(b, 0, 2);
   panvk_per_arch(cs_next_iter_sb)(cmdbuf, PANVK_SUBQUEUE_COMPUTE,
                                   next_iter_sb_scratch);

   unsigned task_axis = MALI_TASK_AXIS_X;
   unsigned task_increment = 0;
   panvk_per_arch(calculate_task_axis_and_increment)(
      shader, phys_dev, &task_axis, &task_increment);
   cs_trace_run_compute(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                        task_increment, task_axis,
                        cs_shader_res_sel(0, 0, 0, 0));

   if (barrier & PANLIB_BARRIER_CSF_SYNC) {
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
                                cs_defer(SB_WAIT_ITER(x),
                                         SB_ID(DEFERRED_SYNC)));
      }
#endif

      ++cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].relative_sync_point;
   } else if (barrier & PANLIB_BARRIER_CSF_WAIT) {
#if PAN_ARCH >= 11
      cs_wait_indirect(b);
#else
      struct cs_index iter_sb = cs_scratch_reg32(b, 0);
      struct cs_index cmp_scratch = cs_scratch_reg32(b, 1);

      cs_load32_to(b, iter_sb, cs_subqueue_ctx_reg(b),
                   offsetof(struct panvk_cs_subqueue_context, iter_sb));

      cs_match(b, iter_sb, cmp_scratch) {
#define CASE(x)                                                                \
      cs_case(b, SB_ITER(x)) {                                                 \
         cs_wait_slot(b, SB_ITER(x));                                          \
      }

         CASE(0)
         CASE(1)
         CASE(2)
         CASE(3)
         CASE(4)
#undef CASE
      }
#endif
   }

   /* XXX: clobbers the registers instead to avoid recreating them when calling
    * a dispatch after? */
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);
}
