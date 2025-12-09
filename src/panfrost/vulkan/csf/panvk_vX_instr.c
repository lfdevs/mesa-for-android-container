/*
 * Copyright 2025 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_instr.h"
#include "panvk_tracepoints.h"
#include "panvk_utrace.h"

#include "util/macros.h"

static void
panvk_instr_end_barrier(enum panvk_subqueue_id id,
                        struct panvk_utrace_cs_info *cs_info,
                        const struct panvk_instr_end_args *const args)
{
   trace_end_barrier(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                     args->barrier.wait_sb_mask,
                     args->barrier.wait_subqueue_mask);
}

static void
panvk_instr_end_cmdbuf(enum panvk_subqueue_id id,
                       struct panvk_utrace_cs_info *cs_info,
                       const struct panvk_instr_end_args *const args)
{
   trace_end_cmdbuf(&cs_info->cmdbuf->utrace.uts[id], cs_info, args->cmdbuf.flags);
}

static void
panvk_instr_end_render(enum panvk_subqueue_id id,
                       struct panvk_utrace_cs_info *cs_info,
                       const struct panvk_instr_end_args *const args)
{
   trace_end_render(&cs_info->cmdbuf->utrace.uts[id], cs_info, args->render.flags,
                    args->render.fb);
}

static void
panvk_instr_end_dispatch(enum panvk_subqueue_id id,
                         struct panvk_utrace_cs_info *cs_info,
                         const struct panvk_instr_end_args *const args)
{
   trace_end_dispatch(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                      args->dispatch.base_group_x, args->dispatch.base_group_y,
                      args->dispatch.base_group_z, args->dispatch.group_count_x,
                      args->dispatch.group_count_y,
                      args->dispatch.group_count_z, args->dispatch.group_size_x,
                      args->dispatch.group_size_y, args->dispatch.group_size_z);
}

static void
panvk_instr_end_dispatch_indirect(enum panvk_subqueue_id id,
                                  struct panvk_utrace_cs_info *cs_info,
                                  const struct panvk_instr_end_args *const args)
{
   trace_end_dispatch_indirect(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                               (struct u_trace_address){
                                  .bo = NULL,
                                  .offset = args->dispatch_indirect.buffer_gpu,
                               });
}

static void
panvk_instr_end_flush_cache(enum panvk_subqueue_id id,
                            struct panvk_utrace_cs_info *cs_info,
                            const struct panvk_instr_end_args *const args)
{
   trace_end_flush_cache(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                         args->flush_cache.l2, args->flush_cache.lsc,
                         args->flush_cache.other);
}

static void
panvk_instr_end_sync32_add(enum panvk_subqueue_id id,
                           struct panvk_utrace_cs_info *cs_info,
                           const struct panvk_instr_end_args *const args)
{
   assert(args->sync.val_regs.size == 1);
   trace_end_sync32_add(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                        (struct u_trace_address){
                           .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                           .offset = args->sync.addr_regs.reg,
                        },
                        (struct u_trace_address){
                           .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                           .offset = args->sync.val_regs.reg,
                        });
}

static void
panvk_instr_end_sync64_add(enum panvk_subqueue_id id,
                           struct panvk_utrace_cs_info *cs_info,
                           const struct panvk_instr_end_args *const args)
{
   assert(args->sync.val_regs.size == 2);
   trace_end_sync64_add(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                        (struct u_trace_address){
                           .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                           .offset = args->sync.addr_regs.reg,
                        },
                        (struct u_trace_address){
                           .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                           .offset = args->sync.val_regs.reg,
                        });
}

static void
panvk_instr_end_sync32_wait(enum panvk_subqueue_id id,
                            struct panvk_utrace_cs_info *cs_info,
                            const struct panvk_instr_end_args *const args)
{
   assert(args->sync.val_regs.size == 1);
   trace_end_sync32_wait(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                         (struct u_trace_address){
                            .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                            .offset = args->sync.addr_regs.reg,
                         },
                         (struct u_trace_address){
                            .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                            .offset = args->sync.val_regs.reg,
                         },
                         args->sync.cond);
}

static void
panvk_instr_end_sync64_wait(enum panvk_subqueue_id id,
                            struct panvk_utrace_cs_info *cs_info,
                            const struct panvk_instr_end_args *const args)
{
   assert(args->sync.val_regs.size == 2);
   trace_end_sync64_wait(&cs_info->cmdbuf->utrace.uts[id], cs_info,
                         (struct u_trace_address){
                            .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                            .offset = args->sync.addr_regs.reg,
                         },
                         (struct u_trace_address){
                            .bo = (void *)PANVK_UTRACE_CAPTURE_REGISTERS,
                            .offset = args->sync.val_regs.reg,
                         },
                         args->sync.cond);
}

void
panvk_per_arch(panvk_instr_begin_work)(enum panvk_subqueue_id id,
                                       struct panvk_cmd_buffer *cmdbuf,
                                       enum panvk_instr_work_type work_type)
{
   struct cs_async_op op = cs_now();
   struct panvk_utrace_cs_info cs_info = {
      .cmdbuf = cmdbuf,
      /* For the begin marker, the caller should wait for dependencies before
         calling begin. */
      .ts_async_op = &op,
   };

   switch (work_type) {
   case PANVK_INSTR_WORK_TYPE_CMDBUF:
      trace_begin_cmdbuf(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_META:
      trace_begin_meta(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_RENDER:
      trace_begin_render(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_DISPATCH:
      trace_begin_dispatch(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_DISPATCH_INDIRECT:
      trace_begin_dispatch_indirect(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_BARRIER:
      trace_begin_barrier(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_FLUSH_CACHE:
      trace_begin_flush_cache(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC32_ADD:
      trace_begin_sync32_add(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC64_ADD:
      trace_begin_sync64_add(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC32_WAIT:
      trace_begin_sync32_wait(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC64_WAIT:
      trace_begin_sync64_wait(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   default:
      UNREACHABLE("unsupported panvk_instr_work_type");
   }
}

void
panvk_per_arch(panvk_instr_end_work)(
   enum panvk_subqueue_id id, struct panvk_cmd_buffer *cmdbuf,
   enum panvk_instr_work_type work_type,
   const struct panvk_instr_end_args *const args)
{
   panvk_per_arch(panvk_instr_end_work_async)(id, cmdbuf, work_type, args,
                                              cs_now());
}

void
panvk_per_arch(panvk_instr_end_work_async)(
   enum panvk_subqueue_id id, struct panvk_cmd_buffer *cmdbuf,
   enum panvk_instr_work_type work_type,
   const struct panvk_instr_end_args *const args,
   struct cs_async_op ts_async_op)
{
   struct panvk_utrace_cs_info cs_info = {
      .cmdbuf = cmdbuf,
      .ts_async_op = &ts_async_op,
   };

   switch (work_type) {
   case PANVK_INSTR_WORK_TYPE_CMDBUF:
      panvk_instr_end_cmdbuf(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_META:
      trace_end_meta(&cmdbuf->utrace.uts[id], &cs_info);
      break;
   case PANVK_INSTR_WORK_TYPE_RENDER:
      panvk_instr_end_render(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_DISPATCH:
      panvk_instr_end_dispatch(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_DISPATCH_INDIRECT:
      panvk_instr_end_dispatch_indirect(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_BARRIER:
      panvk_instr_end_barrier(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_FLUSH_CACHE:
      panvk_instr_end_flush_cache(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC32_ADD:
      panvk_instr_end_sync32_add(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC64_ADD:
      panvk_instr_end_sync64_add(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC32_WAIT:
      panvk_instr_end_sync32_wait(id, &cs_info, args);
      break;
   case PANVK_INSTR_WORK_TYPE_SYNC64_WAIT:
      panvk_instr_end_sync64_wait(id, &cs_info, args);
      break;
   default:
      UNREACHABLE("unsupported panvk_instr_work_type");
   }
}
