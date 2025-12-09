/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_BUFFER_H
#define PANVK_CMD_BUFFER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "genxml/cs_builder.h"

#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_dispatch.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_queue.h"

#include "vk_command_buffer.h"
#include "vk_synchronization.h"

#include "util/list.h"
#include "util/perf/u_trace.h"

struct panvk_sync_scope {
   VkPipelineStageFlags2 stages;
   VkAccessFlags2 access;
};

#define MAX_VBS 16
#define MAX_RTS 8
#define MAX_LAYERS_PER_TILER_DESC 8

struct panvk_cs_sync32 {
   uint32_t seqno;
   uint32_t error;
};

struct panvk_cs_sync64 {
   uint64_t seqno;
   uint32_t error;
   uint32_t pad;
};

struct panvk_cs_desc_ringbuf {
   uint64_t syncobj;
   uint64_t ptr;
   uint32_t pos;
   uint32_t pad;
};

enum panvk_incremental_rendering_pass {
   PANVK_IR_FIRST_PASS,
   PANVK_IR_MIDDLE_PASS,
   PANVK_IR_LAST_PASS,
   PANVK_IR_PASS_COUNT
};

struct panvk_ir_fbd_info {
   uint32_t word0;
   uint32_t word6;
   uint32_t word7;
   uint32_t word12;
};

struct panvk_ir_desc_info {
   struct panvk_ir_fbd_info fbd;
   uint32_t crc_zs_word0;
   uint32_t rtd_word1[MAX_RTS];
};

static inline uint32_t
get_tiler_oom_handler_idx(bool has_zs_ext, uint32_t rt_count)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);
   uint32_t idx = has_zs_ext * MAX_RTS + (rt_count - 1);
   assert(idx < 2 * MAX_RTS);
   return idx;
}

static inline uint32_t
get_fbd_size(bool has_zs_ext, uint32_t rt_count)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);
   uint32_t fbd_size = pan_size(FRAMEBUFFER);
   if (has_zs_ext)
      fbd_size += pan_size(ZS_CRC_EXTENSION);
   fbd_size += pan_size(RENDER_TARGET) * rt_count;
   return fbd_size;
}

/* 512k of render descriptors that can be used when
 * VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT is set on the command buffer. */
#define RENDER_DESC_RINGBUF_SIZE (512 * 1024)

/* Helper defines to get specific fields in the tiler_oom_ctx. */
#define TILER_OOM_CTX_FIELD_OFFSET(_name)                                      \
   offsetof(struct panvk_cs_subqueue_context, tiler_oom_ctx._name)

struct panvk_cs_timestamp_query {
   struct cs_single_link_list_node node;
   uint64_t reports;
   uint64_t avail;
};

struct panvk_cs_occlusion_query {
   struct cs_single_link_list_node node;
   uint64_t syncobj;
};

struct panvk_cs_subqueue_context {
   uint64_t syncobjs;
#if PAN_ARCH == 10
   /* must follow syncobjs immediately for cs_load_to */
   uint32_t iter_sb;
#else
   uint32_t pad;
#endif
   uint32_t last_error;
   uint64_t reg_dump_addr;
   struct {
      struct panvk_cs_desc_ringbuf desc_ringbuf;
      uint64_t tiler_heap;
      uint64_t geom_buf;
      struct cs_single_link_list oq_chain;
      /* Timestamp queries that need to happen after the current rp. */
      struct cs_single_link_list ts_chain;
      struct cs_single_link_list ts_done_chain;
   } render;
   struct {
      uint32_t counter;
      /* Base pointer to regular FBD for layer 0 */
      uint64_t layer_fbd_ptr;
      /* Pointer to scratch FBD used in the event of IR */
      uint64_t ir_scratch_fbd_ptr;
      /* Partial descriptor data needed in the event of IR */
      struct panvk_ir_desc_info ir_desc_infos[PANVK_IR_PASS_COUNT];
      uint32_t td_count;
      uint32_t layer_count;
   } tiler_oom_ctx;
   struct {
      struct {
         uint64_t cs;
      } tracebuf;
   } debug;
} __attribute__((aligned(64)));

struct panvk_cache_flush_info {
   enum mali_cs_flush_mode l2;
   enum mali_cs_flush_mode lsc;
   enum mali_cs_other_flush_mode others;
};

struct panvk_cs_deps {
   bool needs_draw_flush;
   struct {
      uint32_t wait_sb_mask;
      struct panvk_cache_flush_info cache_flush;
   } src[PANVK_SUBQUEUE_COUNT];

   struct {
      uint32_t wait_subqueue_mask;
      bool conditional;
      enum mali_cs_condition cond;
      struct cs_index cond_value;
   } dst[PANVK_SUBQUEUE_COUNT];
   bool needs_layout_transitions;
};

enum panvk_sb_ids {
   PANVK_SB_LS = 0,
   PANVK_SB_IMM_FLUSH = 0,
   PANVK_SB_DEFERRED_SYNC = 1,
   PANVK_SB_DEFERRED_FLUSH = 2,
   PANVK_SB_ITER_START = 3,
   PANVK_SB_ITER_COUNT = 5,
};

#define SB_IMM_MASK     0
#define SB_MASK(nm)     BITFIELD_BIT(PANVK_SB_##nm)
#define SB_ID(nm)       PANVK_SB_##nm
#define SB_ITER(x)      (PANVK_SB_ITER_START + (x))
#define SB_WAIT_ITER(x) BITFIELD_BIT(PANVK_SB_ITER_START + (x))

enum panvk_cs_regs {
   /* RUN_IDVS staging regs. */
   PANVK_CS_REG_RUN_IDVS_SR_START = 0,

#if PAN_ARCH >= 12
   PANVK_CS_REG_RUN_IDVS_SR_END = 65,
#elif PAN_ARCH == 11
   PANVK_CS_REG_RUN_IDVS_SR_END = 63,
#else
   PANVK_CS_REG_RUN_IDVS_SR_END = 60,
#endif

   /* RUN_FRAGMENT staging regs.
    * SW ABI:
    * - r38:39 contain the pointer to the first tiler descriptor. This is
    *   needed to gather completed heap chunks after a run_fragment.
    */
   PANVK_CS_REG_RUN_FRAGMENT_SR_START = 38,
   PANVK_CS_REG_RUN_FRAGMENT_SR_END = 46,

   /* RUN_COMPUTE staging regs. */
   PANVK_CS_REG_RUN_COMPUTE_SR_START = 0,
   PANVK_CS_REG_RUN_COMPUTE_SR_END = 39,

   /* Range of registers that can be used to store temporary data on
    * all queues. Note that some queues have extra space they can use
    * as scratch space.*/
   PANVK_CS_REG_SCRATCH_START = 66,

   /* On v12+, we have 128 registers so that gives us way more space to work with */
#if PAN_ARCH >= 12
   PANVK_CS_REG_SCRATCH_END = 115,

   /* Driver context. */
   PANVK_CS_REG_PROGRESS_SEQNO_START = 116,
   PANVK_CS_REG_PROGRESS_SEQNO_END = 121,
   PANVK_CS_REG_SUBQUEUE_CTX_START = 122,
   PANVK_CS_REG_SUBQUEUE_CTX_END = 123,
#else
   PANVK_CS_REG_SCRATCH_END = 83,

   /* Driver context. */
   PANVK_CS_REG_PROGRESS_SEQNO_START = 84,
   PANVK_CS_REG_PROGRESS_SEQNO_END = 89,
   PANVK_CS_REG_SUBQUEUE_CTX_START = 90,
   PANVK_CS_REG_SUBQUEUE_CTX_END = 91,
#endif
};

#define CS_REG_SCRATCH_COUNT                                                   \
   (PANVK_CS_REG_SCRATCH_END - PANVK_CS_REG_SCRATCH_START + 1)

static inline struct cs_index
cs_scratch_reg_tuple(struct cs_builder *b, unsigned start, unsigned count)
{
   assert(start + count <= CS_REG_SCRATCH_COUNT);
   return cs_reg_tuple(b, PANVK_CS_REG_SCRATCH_START + start, count);
}

static inline struct cs_index
cs_scratch_reg32(struct cs_builder *b, unsigned reg)
{
   return cs_scratch_reg_tuple(b, reg, 1);
}

static inline struct cs_index
cs_scratch_reg64(struct cs_builder *b, unsigned reg)
{
   assert(reg % 2 == 0);
   return cs_scratch_reg_tuple(b, reg, 2);
}

static inline struct cs_index
cs_subqueue_ctx_reg(struct cs_builder *b)
{
   return cs_reg64(b, PANVK_CS_REG_SUBQUEUE_CTX_START);
}

static inline struct cs_index
cs_progress_seqno_reg(struct cs_builder *b, enum panvk_subqueue_id subqueue)
{
   assert(PANVK_CS_REG_PROGRESS_SEQNO_START + (subqueue * 2) <
          PANVK_CS_REG_PROGRESS_SEQNO_END);
   return cs_reg64(b, PANVK_CS_REG_PROGRESS_SEQNO_START + (subqueue * 2));
}

struct panvk_cs_reg_upd_context {
   reg_perm_cb_t reg_perm;
   struct panvk_cs_reg_upd_context *next;
};

struct panvk_cs_state {
   struct cs_builder builder;

   /* Used to debug register writes in invalid contexts. */
   struct {
      struct panvk_cs_reg_upd_context *upd_ctx_stack;
      reg_perm_cb_t base_perm;
   } reg_access;

   /* Sync point relative to the beginning of the command buffer.
    * Needs to be offset with the subqueue sync point. */
   int32_t relative_sync_point;

   struct cs_tracing_ctx tracing;
};

static inline struct panvk_cs_reg_upd_context *
panvk_cs_reg_ctx_push(struct cs_builder *b,
                      struct panvk_cs_reg_upd_context *ctx,
                      reg_perm_cb_t reg_perm)
{
   struct panvk_cs_state *cs_state =
      container_of(b, struct panvk_cs_state, builder);

   ctx->reg_perm = reg_perm;
   ctx->next = cs_state->reg_access.upd_ctx_stack;
   cs_state->reg_access.upd_ctx_stack = ctx;
   return ctx;
}

static inline void
panvk_cs_reg_ctx_pop(struct cs_builder *b, struct panvk_cs_reg_upd_context *ctx)
{
   struct panvk_cs_state *cs_state =
      container_of(b, struct panvk_cs_state, builder);

   assert(cs_state->reg_access.upd_ctx_stack == ctx);

   cs_state->reg_access.upd_ctx_stack = ctx->next;
}

struct panvk_cs_reg_range {
   unsigned start;
   unsigned end;
};

#define PANVK_CS_REG_RANGE(__name)                                             \
   {                                                                           \
      .start = PANVK_CS_REG_##__name##_START,                                  \
      .end = PANVK_CS_REG_##__name##_END,                                      \
   }

#define panvk_cs_reg_blacklist(__name, ...)                                    \
   static inline enum cs_reg_perm panvk_cs_##__name##_reg_perm(                \
      struct cs_builder *b, unsigned reg)                                      \
   {                                                                           \
      const struct panvk_cs_reg_range ranges[] = {                             \
         __VA_ARGS__,                                                          \
      };                                                                       \
                                                                               \
      for (unsigned i = 0; i < ARRAY_SIZE(ranges); i++) {                      \
         if (reg >= ranges[i].start && reg <= ranges[i].end)                   \
            return CS_REG_RD;                                                  \
      }                                                                        \
                                                                               \
      return CS_REG_RW;                                                        \
   }

panvk_cs_reg_blacklist(vt, PANVK_CS_REG_RANGE(RUN_IDVS_SR),
                       PANVK_CS_REG_RANGE(PROGRESS_SEQNO),
                       PANVK_CS_REG_RANGE(SUBQUEUE_CTX));
panvk_cs_reg_blacklist(frag, PANVK_CS_REG_RANGE(RUN_FRAGMENT_SR),
                       PANVK_CS_REG_RANGE(PROGRESS_SEQNO),
                       PANVK_CS_REG_RANGE(SUBQUEUE_CTX));
panvk_cs_reg_blacklist(compute, PANVK_CS_REG_RANGE(RUN_COMPUTE_SR),
                       PANVK_CS_REG_RANGE(PROGRESS_SEQNO),
                       PANVK_CS_REG_RANGE(SUBQUEUE_CTX));

#define panvk_cs_reg_whitelist(__name, ...)                                    \
   static inline enum cs_reg_perm panvk_cs_##__name##_reg_perm(                \
      struct cs_builder *b, unsigned reg)                                      \
   {                                                                           \
      const struct panvk_cs_reg_range ranges[] = {                             \
         __VA_ARGS__,                                                          \
      };                                                                       \
                                                                               \
      for (unsigned i = 0; i < ARRAY_SIZE(ranges); i++) {                      \
         if (reg >= ranges[i].start && reg <= ranges[i].end)                   \
            return CS_REG_RW;                                                  \
      }                                                                        \
                                                                               \
      return CS_REG_RD;                                                        \
   }

#define panvk_cs_reg_upd_ctx(__b, __name)                                      \
   for (struct panvk_cs_reg_upd_context __reg_upd_ctx,                         \
        *reg_upd_ctxp = panvk_cs_reg_ctx_push(__b, &__reg_upd_ctx,             \
                                              panvk_cs_##__name##_reg_perm);   \
        reg_upd_ctxp;                                                          \
        panvk_cs_reg_ctx_pop(__b, &__reg_upd_ctx), reg_upd_ctxp = NULL)

panvk_cs_reg_whitelist(progress_seqno, PANVK_CS_REG_RANGE(PROGRESS_SEQNO));
#define cs_update_progress_seqno(__b) panvk_cs_reg_upd_ctx(__b, progress_seqno)

panvk_cs_reg_whitelist(compute_ctx, PANVK_CS_REG_RANGE(RUN_COMPUTE_SR));
#define cs_update_compute_ctx(__b) panvk_cs_reg_upd_ctx(__b, compute_ctx)

panvk_cs_reg_whitelist(frag_ctx, PANVK_CS_REG_RANGE(RUN_FRAGMENT_SR));
#define cs_update_frag_ctx(__b) panvk_cs_reg_upd_ctx(__b, frag_ctx)

panvk_cs_reg_whitelist(vt_ctx, PANVK_CS_REG_RANGE(RUN_IDVS_SR));
#define cs_update_vt_ctx(__b) panvk_cs_reg_upd_ctx(__b, vt_ctx)

panvk_cs_reg_whitelist(cmdbuf_regs, {PANVK_CS_REG_RUN_IDVS_SR_START,
                                     PANVK_CS_REG_SCRATCH_END});
#define cs_update_cmdbuf_regs(__b) panvk_cs_reg_upd_ctx(__b, cmdbuf_regs)

struct panvk_tls_state {
   struct pan_ptr desc;
   struct pan_tls_info info;
   unsigned max_wg_count;
};

struct panvk_cmd_buffer {
   struct vk_command_buffer vk;
   VkCommandBufferUsageFlags flags;
   struct panvk_pool cs_pool;
   struct panvk_pool desc_pool;
   struct panvk_pool tls_pool;
   struct list_head push_sets;

   struct {
      struct u_trace uts[PANVK_SUBQUEUE_COUNT];
   } utrace;

   struct {
      struct panvk_cmd_graphics_state gfx;
      struct panvk_cmd_compute_state compute;
      struct panvk_push_constant_state push_constants;
      struct panvk_cs_state cs[PANVK_SUBQUEUE_COUNT];
      struct panvk_tls_state tls;
      bool contains_timestamp_queries;
   } state;
};

VK_DEFINE_HANDLE_CASTS(panvk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

static bool
inherits_render_ctx(struct panvk_cmd_buffer *cmdbuf)
{
   return (cmdbuf->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
           (cmdbuf->flags &
            VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) ||
          (cmdbuf->state.gfx.render.flags & VK_RENDERING_RESUMING_BIT);
}

static inline struct cs_builder *
panvk_get_cs_builder(struct panvk_cmd_buffer *cmdbuf, uint32_t subqueue)
{
   return &cmdbuf->state.cs[subqueue].builder;
}

static inline struct panvk_descriptor_state *
panvk_cmd_get_desc_state(struct panvk_cmd_buffer *cmdbuf,
                         VkPipelineBindPoint bindpoint)
{
   switch (bindpoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmdbuf->state.gfx.desc_state;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmdbuf->state.compute.desc_state;

   default:
      assert(!"Unsupported bind point");
      return NULL;
   }
}

static bool
panvk_cache_flush_is_nop(const struct panvk_cache_flush_info *cache_flush)
{
   return cache_flush->l2 == MALI_CS_FLUSH_MODE_NONE &&
          cache_flush->lsc == MALI_CS_FLUSH_MODE_NONE &&
          cache_flush->others == MALI_CS_OTHER_FLUSH_MODE_NONE;
}

extern const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops);

void panvk_per_arch(cmd_flush_draws)(struct panvk_cmd_buffer *cmdbuf);

#if PAN_ARCH == 10
/* Match against all possible iter_sb values. The constant iter_sb value for
 * the current match arm is in '__val'. */
#define cs_match_iter_sb(__b, __val, __iter_sb, __scratch)                     \
   cs_match(__b, __iter_sb, __scratch)                                         \
      for (unsigned __val = 0; __val < PANVK_SB_ITER_COUNT; __val++)           \
         cs_case(__b, SB_ITER(__val))
#endif

void panvk_per_arch(cs_next_iter_sb)(struct panvk_cmd_buffer *cmdbuf,
                                     enum panvk_subqueue_id subqueue,
                                     struct cs_index scratch_regs);

enum panvk_barrier_stage {
   PANVK_BARRIER_STAGE_FIRST,
   PANVK_BARRIER_STAGE_AFTER_LAYOUT_TRANSITION,
};

void panvk_per_arch(add_cs_deps)(
   struct panvk_cmd_buffer *cmdbuf,
   enum panvk_barrier_stage barrier_stage,
   const VkDependencyInfo *in,
   struct panvk_cs_deps *out,
   bool is_set_event);

VkResult panvk_per_arch(cmd_prepare_exec_cmd_for_draws)(
   struct panvk_cmd_buffer *primary, struct panvk_cmd_buffer *secondary);

void panvk_per_arch(cmd_inherit_render_state)(
   struct panvk_cmd_buffer *cmdbuf, const VkCommandBufferBeginInfo *pBeginInfo);

static inline void
panvk_per_arch(calculate_task_axis_and_increment)(
   const struct panvk_shader_variant *shader,
   struct panvk_physical_device *phys_dev, unsigned *task_axis,
   unsigned *task_increment)
{
   /* Pick the task_axis and task_increment to maximize thread
    * utilization. */
   unsigned threads_per_wg = shader->cs.local_size.x * shader->cs.local_size.y *
                             shader->cs.local_size.z;
   unsigned max_thread_cnt = pan_compute_max_thread_count(
      &phys_dev->kmod.props, shader->info.work_reg_count);
   unsigned threads_per_task = threads_per_wg;
   unsigned local_size[3] = {
      shader->cs.local_size.x,
      shader->cs.local_size.y,
      shader->cs.local_size.z,
   };

   for (unsigned i = 0; i < 3; i++) {
      if (threads_per_task * local_size[i] >= max_thread_cnt) {
         /* We reached out thread limit, stop at the current axis and
          * calculate the increment so it doesn't exceed the per-core
          * thread capacity.
          */
         *task_increment = max_thread_cnt / threads_per_task;
         break;
      } else if (*task_axis == MALI_TASK_AXIS_Z) {
         /* We reached the Z axis, and there's still room to stuff more
          * threads. Pick the current axis grid size as our increment
          * as there's no point using something bigger.
          */
         *task_increment = local_size[i];
         break;
      }

      threads_per_task *= local_size[i];
      (*task_axis)++;
   }

   assert(*task_axis <= MALI_TASK_AXIS_Z);
   assert(*task_increment > 0);
}

static VkPipelineStageFlags2
panvk_get_subqueue_stages(enum panvk_subqueue_id subqueue)
{
   switch (subqueue) {
   case PANVK_SUBQUEUE_VERTEX_TILER:
      return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
             VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
             VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
             VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
   case PANVK_SUBQUEUE_FRAGMENT:
      return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
             VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT |
             VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
   case PANVK_SUBQUEUE_COMPUTE:
      return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
             VK_PIPELINE_STAGE_2_COPY_BIT;
   default:
      UNREACHABLE("Invalid subqueue id");
   }
}

/* Hint for vk_stages_to_subqueue_mask if stages refer to first or second
 * synchronization scope
 */
enum sync_scope {
   /* First scope (sometimes called 'source' in Vulkan)
    * is the first stage(s) of an execution dependency chain.
    */
   SYNC_SCOPE_FIRST,
   /* Second scope (sometimes called 'destination' in Vulkan)
    * is the second stage(s) of an execution dependency chain.
    */
   SYNC_SCOPE_SECOND,
};

static uint32_t
vk_stages_to_subqueue_mask(VkPipelineStageFlags2 vk_stages,
                           enum sync_scope scope)
{
   /* Handle other compound stages by expanding. */
   switch (scope) {
   case SYNC_SCOPE_FIRST:
      vk_stages = vk_expand_src_stage_flags2(vk_stages);
      break;
   case SYNC_SCOPE_SECOND:
      vk_stages = vk_expand_dst_stage_flags2(vk_stages);
      break;
   default:
      UNREACHABLE("Invalid sync_scope");
   }

   VkPipelineStageFlags2 flags[PANVK_SUBQUEUE_COUNT];
   for (uint32_t sq = 0; sq < PANVK_SUBQUEUE_COUNT; ++sq)
      flags[sq] = panvk_get_subqueue_stages(sq);

   uint32_t result = 0;
   if (flags[PANVK_SUBQUEUE_VERTEX_TILER] & vk_stages)
      result |= BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER);

   if (flags[PANVK_SUBQUEUE_FRAGMENT] & vk_stages)
      result |= BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT);

   if (flags[PANVK_SUBQUEUE_COMPUTE] & vk_stages)
      result |= BITFIELD_BIT(PANVK_SUBQUEUE_COMPUTE);

   return result;
}

void panvk_per_arch(emit_barrier)(struct panvk_cmd_buffer *cmdbuf,
                                  struct panvk_cs_deps deps);
#if PAN_ARCH >= 10

void panvk_per_arch(cs_patch_ir_state)(
   struct cs_builder *b, const struct cs_tracing_ctx *tracing_ctx,
   bool has_zs_ext, uint32_t rt_count, struct cs_index remaining_layers_in_td,
   struct cs_index current_fbd_ptr_reg, struct cs_index ir_desc_info_ptr,
   struct cs_index ir_fbd_word_0, struct cs_index scratch_fbd_ptr_reg,
   struct cs_index scratch_registers_5);

void panvk_per_arch(cs_ir_update_registers_to_next_layer)(
   struct cs_builder *b, bool has_zs_ext, uint32_t rt_count,
   struct cs_index current_fbd_ptr_reg, struct cs_index ir_fbd_word_0,
   struct cs_index remaining_layers_in_td);
#endif /* PAN_ARCH >= 10 */

#endif /* PANVK_CMD_BUFFER_H */
