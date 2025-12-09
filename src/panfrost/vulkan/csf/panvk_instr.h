/*
 * Copyright 2025 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_queue.h"

enum panvk_instr_work_type {
   PANVK_INSTR_WORK_TYPE_CMDBUF,
   PANVK_INSTR_WORK_TYPE_META,
   PANVK_INSTR_WORK_TYPE_RENDER,
   PANVK_INSTR_WORK_TYPE_DISPATCH,
   PANVK_INSTR_WORK_TYPE_DISPATCH_INDIRECT,
   PANVK_INSTR_WORK_TYPE_BARRIER,
   PANVK_INSTR_WORK_TYPE_FLUSH_CACHE,
   PANVK_INSTR_WORK_TYPE_SYNC32_ADD,
   PANVK_INSTR_WORK_TYPE_SYNC64_ADD,
   PANVK_INSTR_WORK_TYPE_SYNC32_WAIT,
   PANVK_INSTR_WORK_TYPE_SYNC64_WAIT,
};

struct panvk_instr_end_args {
   /* Depending on which work type is ended, one of the options below is valid. */
   union {
      struct {
         uint8_t wait_sb_mask;
         uint8_t wait_subqueue_mask;
      } barrier;

      struct {
         VkCommandBufferUsageFlags flags;
      } cmdbuf;

      struct {
         VkRenderingFlags flags;
         const struct pan_fb_info *fb;
      } render;

      struct {
         uint16_t base_group_x;
         uint16_t base_group_y;
         uint16_t base_group_z;
         uint16_t group_count_x;
         uint16_t group_count_y;
         uint16_t group_count_z;
         uint16_t group_size_x;
         uint16_t group_size_y;
         uint16_t group_size_z;
      } dispatch;

      struct {
         uint64_t buffer_gpu;
      } dispatch_indirect;

      struct {
         uint8_t l2;
         uint8_t lsc;
         uint8_t other;
      } flush_cache;

      struct {
         struct cs_index addr_regs;
         struct cs_index val_regs;
         enum mali_cs_condition cond;
      } sync;
   };
};

void
   panvk_per_arch(panvk_instr_begin_work)(enum panvk_subqueue_id id,
                                          struct panvk_cmd_buffer *cmdbuf,
                                          enum panvk_instr_work_type work_type);

/**
 * Mark the end of synchronous work.
 */
void panvk_per_arch(panvk_instr_end_work)(
   enum panvk_subqueue_id id, struct panvk_cmd_buffer *cmdbuf,
   enum panvk_instr_work_type work_type,
   const struct panvk_instr_end_args *const args);

/**
 * Mark the end of async work with an async_op. Note that the signal_slot will
 * be overwritten and should therefore be left as 0.
 */
void panvk_per_arch(panvk_instr_end_work_async)(
   enum panvk_subqueue_id id, struct panvk_cmd_buffer *cmdbuf,
   enum panvk_instr_work_type work_type,
   const struct panvk_instr_end_args *const args,
   struct cs_async_op ts_async_op);

#define PANVK_INSTR_SYNC_OPS(__cnt_width)                                      \
   static inline void panvk_instr_sync##__cnt_width##_add(                     \
      struct panvk_cmd_buffer *cmdbuf, enum panvk_subqueue_id id,              \
      bool propagate_error, enum mali_cs_sync_scope scope,                     \
      struct cs_index val, struct cs_index addr, struct cs_async_op async)     \
   {                                                                           \
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, id);                 \
      panvk_per_arch(panvk_instr_begin_work)(                                  \
         id, cmdbuf, PANVK_INSTR_WORK_TYPE_SYNC##__cnt_width##_ADD);           \
      cs_sync##__cnt_width##_add(b, propagate_error, scope, val, addr, async); \
      struct panvk_instr_end_args instr_info = {                               \
         .sync = {.addr_regs = addr, .val_regs = val},                         \
      };                                                                       \
      async.signal_slot = 0;                                                   \
      panvk_per_arch(panvk_instr_end_work_async)(                              \
         id, cmdbuf, PANVK_INSTR_WORK_TYPE_SYNC##__cnt_width##_ADD,            \
         &instr_info, async);                                                  \
   }                                                                           \
                                                                               \
   static inline void panvk_instr_sync##__cnt_width##_wait(                    \
      struct panvk_cmd_buffer *cmdbuf, enum panvk_subqueue_id id,              \
      bool reject_error, enum mali_cs_condition cond, struct cs_index ref,     \
      struct cs_index addr)                                                    \
   {                                                                           \
      struct cs_builder *b = panvk_get_cs_builder(cmdbuf, id);                 \
      panvk_per_arch(panvk_instr_begin_work)(                                  \
         id, cmdbuf, PANVK_INSTR_WORK_TYPE_SYNC##__cnt_width##_WAIT);          \
      cs_sync##__cnt_width##_wait(b, reject_error, cond, ref, addr);           \
      struct panvk_instr_end_args instr_info = {                               \
         .sync = {.addr_regs = addr, .val_regs = ref, .cond = cond},           \
      };                                                                       \
      panvk_per_arch(panvk_instr_end_work)(                                    \
         id, cmdbuf, PANVK_INSTR_WORK_TYPE_SYNC##__cnt_width##_WAIT,           \
         &instr_info);                                                         \
   }

PANVK_INSTR_SYNC_OPS(32)
PANVK_INSTR_SYNC_OPS(64)
