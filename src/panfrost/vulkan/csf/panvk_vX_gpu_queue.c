/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2026 NXP
 * SPDX-License-Identifier: MIT
 */

#include "drm-uapi/panthor_drm.h"

#include "genxml/cs_builder.h"
#include "genxml/decode.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device_memory.h"
#include "panvk_macros.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"
#include "panvk_queue.h"
#include "panvk_utrace.h"

#include "pan_trace.h"

#include "kmod/pan_kmod.h"

#include "util/bitscan.h"
#include "util/os_time.h"
#include "vk_drm_syncobj.h"
#include "vk_log.h"
#include "vk_sync.h"

#ifdef HAVE_PAN_KMOD_KBASE
#include <inttypes.h>
#include <unistd.h>
#include "drm-uapi/mali_kbase_ioctl.h"
#include "kmod/kbase_kmod.h"
#endif

#define MIN_DESC_TRACEBUF_SIZE (128 * 1024)
#define DEFAULT_DESC_TRACEBUF_SIZE (2 * 1024 * 1024)
#define MIN_CS_TRACEBUF_SIZE (512 * 1024)
#define DEFAULT_CS_TRACEBUF_SIZE (2 * 1024 * 1024)

#ifdef HAVE_PAN_KMOD_KBASE

/* kbase-backed queue support.
 *
 * On panthor, the kernel owns a per-queue ring buffer: every
 * drm_panthor_queue_submit makes the kernel emit a small
 * flush+CALL+sync sequence into that ring.  kbase has no such
 * per-submission ioctl — userspace owns the ring — so we emit the
 * equivalent sequence ourselves, publish the new insert offset through
 * the USER_IO input page and kick the scheduler.
 *
 * Completion is tracked with per-subqueue GPU seqnos.  Vulkan sync objects
 * retain a snapshot of those seqnos and only wait when the application asks
 * for completion; ordinary queue submission remains asynchronous.
 */

#define KBASE_RINGBUF_SIZE     (64 * 1024)
/* Worst-case size of one ring entry, in bytes. Diagnostic breadcrumbs add a
 * few extra LS stores, so keep this conservatively above the normal panthor
 * kernel ring slot size. */
#define KBASE_RING_JOB_MAX_SIZE 512
/* Generous timeout for the synchronous submission model. */
#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)
#define KBASE_SEQNO_LS_COPY_OFFSET       16
#define KBASE_SEQNO_MARK_PRE_CALL_OFFSET 24
#define KBASE_SEQNO_MARK_POST_CALL_OFFSET 32
#define KBASE_SEQNO_MARK_POST_WAIT_OFFSET 40
#define KBASE_SEQNO_STREAM_PROGRESS_OFFSET 48
#define KBASE_SEQNO_MARK_PRE_CALL  0x100000000000ull
#define KBASE_SEQNO_MARK_POST_CALL 0x200000000000ull
#define KBASE_SEQNO_MARK_POST_WAIT 0x300000000000ull
#define KBASE_CACHELINE_SIZE 64
/* With firmware chunk recycling disarmed (no FRAGMENT_COMPLETED heap ops on
 * kbase), the heap only grows between renewals.  Light submissions can share
 * a heap generation, while command buffers with enough estimated tiler work
 * force renewal after the current submission.  This keeps Minecraft's large
 * frames within the heap limit without draining the queues after every small
 * vkmark/vkcube frame. */
#define KBASE_TILER_HEAP_RENEW_INTERVAL 128
#define KBASE_TILER_HEAP_RENEW_WORK 65536

/* Diagnostic override for the tiler-heap renewal cadence.
 * PANVK_KBASE_HEAP_RENEW_INTERVAL=0 disables renewal entirely; any positive
 * value replaces the default interval. */
static uint32_t
kbase_tiler_heap_renew_interval(void)
{
   static uint32_t interval;

   if (!interval) {
      int64_t v = debug_get_num_option("PANVK_KBASE_HEAP_RENEW_INTERVAL",
                                       KBASE_TILER_HEAP_RENEW_INTERVAL);
      interval = (v <= 0 || v > UINT32_MAX) ? UINT32_MAX : (uint32_t)v;
   }

   return interval;
}

static uint64_t
kbase_tiler_heap_renew_work(void)
{
   static uint64_t threshold = UINT64_MAX;

   if (threshold == UINT64_MAX) {
      int64_t v = debug_get_num_option("PANVK_KBASE_HEAP_RENEW_WORK",
                                       KBASE_TILER_HEAP_RENEW_WORK);
      threshold = v > 0 ? v : 0;
   }

   return threshold;
}

static VkResult
kbase_subqueue_wait_seqno(struct panvk_gpu_queue *queue, uint32_t subqueue,
                          uint64_t target_seqno, uint32_t rekick_mask,
                          bool allow_ring_drain, uint64_t abs_timeout_ns);

static bool
gpu_queue_uses_kbase(const struct panvk_device *dev)
{
   return to_panvk_physical_device(dev->vk.physical)->kbase_node_path[0] !=
          '\0';
}

static uint32_t
kbase_resource_mask(enum panvk_subqueue_id subqueue)
{
   switch (subqueue) {
   case PANVK_SUBQUEUE_VERTEX_TILER:
      return CS_IDVS_RES | CS_TILER_RES;
   case PANVK_SUBQUEUE_FRAGMENT:
      return CS_FRAG_RES;
   case PANVK_SUBQUEUE_COMPUTE:
      return CS_COMPUTE_RES;
   default:
      UNREACHABLE("Unknown subqueue");
   }
}

/* Barrier that drains CPU (write-combine) stores all the way to the point
 * of coherency shared with the GPU before the doorbell/kick is observed.
 * The Mali GPU sits in the outer/system shareability domain, so the inner-
 * shareable DMB that __sync_synchronize() emits on aarch64 is not enough:
 * the ring-buffer writes could still be sitting in the CPU write-combine
 * buffer when the firmware reads the ring from DRAM (it would then execute
 * zero-filled NOPs and idle without touching memory).  DSB SY drains them. */
static inline void
kbase_gpu_wmb(void)
{
#if defined(__aarch64__)
   __asm__ volatile("dsb sy" ::: "memory");
#elif defined(__arm__)
   __asm__ volatile("dsb sy" ::: "memory");
#else
   __sync_synchronize();
#endif
}

static inline void
kbase_cache_clean_range(const void *start, size_t size)
{
#if defined(__aarch64__)
   uintptr_t ptr = (uintptr_t)start & ~(uintptr_t)(KBASE_CACHELINE_SIZE - 1);
   uintptr_t end = ALIGN_POT((uintptr_t)start + size, KBASE_CACHELINE_SIZE);

   for (; ptr < end; ptr += KBASE_CACHELINE_SIZE)
      __asm__ volatile("dc cvac, %0" : : "r"(ptr) : "memory");
#else
   (void)start;
   (void)size;
#endif

   kbase_gpu_wmb();
}

static inline void
kbase_cache_invalidate_range(const void *start, size_t size)
{
#if defined(__aarch64__)
   uintptr_t ptr = (uintptr_t)start & ~(uintptr_t)(KBASE_CACHELINE_SIZE - 1);
   uintptr_t end = ALIGN_POT((uintptr_t)start + size, KBASE_CACHELINE_SIZE);

   for (; ptr < end; ptr += KBASE_CACHELINE_SIZE)
      __asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
#else
   (void)start;
   (void)size;
#endif

   kbase_gpu_wmb();
}

static void
kbase_clean_priv_mem(struct panvk_priv_mem mem, uint64_t offset, size_t size)
{
   void *cpu = panvk_priv_mem_host_addr(mem);

   if (cpu)
      kbase_cache_clean_range((uint8_t *)cpu + offset, size);
}

static uint32_t
kbase_seqno_stride(void)
{
   return ALIGN_POT(sizeof(struct panvk_cs_sync64), 64);
}

static volatile struct panvk_cs_sync64 *
kbase_subqueue_seqno_cell(struct panvk_gpu_queue *queue, uint32_t subqueue)
{
   uint8_t *base = queue->kbase_seqnos.cpu;

   return (volatile struct panvk_cs_sync64 *)(base +
                                              subqueue *
                                                 kbase_seqno_stride());
}

static uint64_t
kbase_subqueue_seqno_dev_addr(struct panvk_gpu_queue *queue, uint32_t subqueue)
{
   return queue->kbase_seqnos.dev + subqueue * kbase_seqno_stride();
}

static uint64_t
kbase_ring_qword(const struct panvk_subqueue *subq, uint64_t byte_offset)
{
   uint64_t off = byte_offset % KBASE_RINGBUF_SIZE;

   return *(volatile uint64_t *)((uint8_t *)subq->kbase.ringbuf_cpu + off);
}

static uint64_t
kbase_stream_qword(const uint64_t *stream, uint32_t size, uint32_t qword)
{
   return qword * sizeof(uint64_t) < size ? stream[qword] : 0;
}

static uint8_t
kbase_stream_opcode(const uint64_t *stream, uint32_t size, uint32_t qword)
{
   return kbase_stream_qword(stream, size, qword) >> 56;
}

static void
kbase_log_queue_syncobjs(struct panvk_gpu_queue *queue)
{
   struct panvk_cs_sync64 *syncobjs = panvk_priv_mem_host_addr(queue->syncobjs);

   if (!syncobjs)
      return;

   kbase_cache_invalidate_range(syncobjs,
                                sizeof(*syncobjs) * PANVK_SUBQUEUE_COUNT);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct panvk_cs_sync64 *syncobj = &syncobjs[i];

      mesa_loge("kbase: queue syncobj %u: seqno %" PRIu64
                ", error 0x%x, pad 0x%x",
                i, syncobj->seqno, syncobj->error, syncobj->pad);
   }
}

static void
kbase_log_stream_prefix(uint32_t subqueue, uint64_t stream_addr,
                        uint32_t stream_size, const uint64_t *stream)
{
   if (!stream || !stream_size)
      return;

   /* Dump the whole stream, not just the first 96 qwords: the CALL wrapper
    * returns when it has executed the callee's full byte length, so the
    * tail (and any trailing sync/return structure) is exactly what we need
    * to see when a CALL never returns. */
   for (uint32_t base = 0; base < stream_size / sizeof(uint64_t);
        base += 8) {
      mesa_logd("kbase: stream subqueue %u 0x%" PRIx64 "/%u qwords[%u..%u] "
                "0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
                "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
                " ops %02x/%02x/%02x/%02x/%02x/%02x/%02x/%02x",
                subqueue, stream_addr, stream_size, base, base + 7,
                kbase_stream_qword(stream, stream_size, base + 0),
                kbase_stream_qword(stream, stream_size, base + 1),
                kbase_stream_qword(stream, stream_size, base + 2),
                kbase_stream_qword(stream, stream_size, base + 3),
                kbase_stream_qword(stream, stream_size, base + 4),
                kbase_stream_qword(stream, stream_size, base + 5),
                kbase_stream_qword(stream, stream_size, base + 6),
                kbase_stream_qword(stream, stream_size, base + 7),
                kbase_stream_opcode(stream, stream_size, base + 0),
                kbase_stream_opcode(stream, stream_size, base + 1),
                kbase_stream_opcode(stream, stream_size, base + 2),
                kbase_stream_opcode(stream, stream_size, base + 3),
                kbase_stream_opcode(stream, stream_size, base + 4),
                kbase_stream_opcode(stream, stream_size, base + 5),
                kbase_stream_opcode(stream, stream_size, base + 6),
                kbase_stream_opcode(stream, stream_size, base + 7));
   }
}

static void
kbase_log_ring_line(const struct panvk_subqueue *subq, uint32_t subqueue,
                    const char *label, uint64_t byte_offset)
{
   uint64_t line = byte_offset & ~(uint64_t)63;

   mesa_loge("kbase: subqueue %u %s ring[0..7] @%" PRIu64
             " 0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
             "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64,
             subqueue, label, line, kbase_ring_qword(subq, line + 0),
             kbase_ring_qword(subq, line + 8),
             kbase_ring_qword(subq, line + 16),
             kbase_ring_qword(subq, line + 24),
             kbase_ring_qword(subq, line + 32),
             kbase_ring_qword(subq, line + 40),
             kbase_ring_qword(subq, line + 48),
             kbase_ring_qword(subq, line + 56));
}

static void
kbase_log_subqueue_state(struct panvk_gpu_queue *queue, uint32_t subqueue,
                         const char *reason)
{
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];

   if (!subq->kbase.user_io || !subq->kbase.ringbuf_cpu)
      return;

   volatile struct panvk_cs_sync64 *cell =
      kbase_subqueue_seqno_cell(queue, subqueue);
   volatile uint64_t *ls_copy =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_LS_COPY_OFFSET);
   volatile uint64_t *mark_pre_call =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_MARK_PRE_CALL_OFFSET);
   volatile uint64_t *mark_post_call =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_MARK_POST_CALL_OFFSET);
   volatile uint64_t *mark_post_wait =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_MARK_POST_WAIT_OFFSET);
   volatile uint32_t *stream_progress =
      (volatile uint32_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_STREAM_PROGRESS_OFFSET);
   const uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
   uint64_t extract =
      *(volatile uint64_t *)(output_page + CS_USER_IO_OUTPUT_CS_EXTRACT);
   uint32_t active =
      *(volatile uint32_t *)(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE);

   kbase_cache_invalidate_range((const void *)cell, kbase_seqno_stride());

   mesa_loge("kbase: %s subqueue %u: seqno %" PRIu64 ", ls_copy %" PRIu64
             ", target %" PRIu64 ", marks 0x%" PRIx64 "/0x%" PRIx64
             "/0x%" PRIx64 ", insert %" PRIu64 ", extract %" PRIu64
             ", active %u, error 0x%x, jobs %" PRIu64,
             reason, subqueue, (uint64_t)cell->seqno, *ls_copy,
             subq->kbase.emitted_jobs, *mark_pre_call, *mark_post_call,
             *mark_post_wait, subq->kbase.insert, extract, active, cell->error,
             subq->kbase.emitted_jobs);
   mesa_loge("kbase: %s subqueue %u stream progress 0x%x", reason,
             subqueue, *stream_progress);

   mesa_loge("kbase: %s subqueue %u last job: ring offset %u, entry "
             "%u/%u bytes, stream 0x%" PRIx64 "/%u, flush %u",
             reason, subqueue, subq->kbase.last_job_offset,
             subq->kbase.last_job_entry_size, subq->kbase.last_job_size,
             subq->kbase.last_stream_addr, subq->kbase.last_stream_size,
             subq->kbase.last_flush_id);

   kbase_log_ring_line(subq, subqueue, "extract", extract);

   if (subq->kbase.last_job_size)
      kbase_log_ring_line(subq, subqueue, "last-job",
                          subq->kbase.last_job_offset);
}

static VkResult
kbase_init_seqnos(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   /* Completion uses SYNC_ADD64, so request CSF event notifications for this
    * BO.  This wakes ppoll() as soon as a pending fence target retires instead
    * of making a waiter sleep until its periodic timeout.  Keep the normal
    * cached memory path: GPU-uncached BOs are not CPU-observable on all
    * Android kbase stacks. */
   queue->kbase_seqnos.bo =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, 4096,
                       PAN_KMOD_BO_FLAG_CSF_EVENT);
   if (!queue->kbase_seqnos.bo)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to allocate kbase seqno cells");

   queue->kbase_seqnos.cpu = pan_kmod_bo_mmap(
      queue->kbase_seqnos.bo, PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
   if (queue->kbase_seqnos.cpu == MAP_FAILED) {
      queue->kbase_seqnos.cpu = NULL;
      return panvk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "Failed to map kbase seqno cells");
   }

   struct pan_kmod_vm_op op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = PAN_KMOD_VM_MAP_AUTO_VA,
         .size = 4096,
      },
      .map = {
         .bo = queue->kbase_seqnos.bo,
         .bo_offset = 0,
      },
   };
   if (pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to GPU map kbase seqno cells");

   queue->kbase_seqnos.dev = op.va.start;
   memset(queue->kbase_seqnos.cpu, 0, 4096);
   kbase_cache_clean_range(queue->kbase_seqnos.cpu, 4096);
   return VK_SUCCESS;
}

/* Emit one job into the subqueue ring: cache maintenance, a CALL to the
 * command stream, then a SYNC_ADD64 on the subqueue seqno cell deferred on
 * all scoreboard slots — the same sequence the panthor kernel driver emits
 * into its kernel-owned rings.  Only the FW-unpreserved registers (the top
 * 4 of the register file) are clobbered, which the rest of the driver
 * stays away from. */
static VkResult
kbase_subqueue_reserve_ring(struct panvk_gpu_queue *queue,
                            uint32_t subqueue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];
   const uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
   uint64_t extract = *(volatile uint64_t *)(output_page +
                                             CS_USER_IO_OUTPUT_CS_EXTRACT);
   uint32_t offset = subq->kbase.insert % KBASE_RINGBUF_SIZE;
   uint64_t required = KBASE_RING_JOB_MAX_SIZE;

   if (offset + KBASE_RING_JOB_MAX_SIZE > KBASE_RINGBUF_SIZE)
      required += KBASE_RINGBUF_SIZE - offset;

   if (subq->kbase.insert - extract + required <= KBASE_RINGBUF_SIZE)
      return VK_SUCCESS;

   VkResult result = kbase_subqueue_wait_seqno(
      queue, subqueue, subq->kbase.emitted_jobs, BITFIELD_BIT(subqueue),
      false, UINT64_MAX);
   if (result != VK_SUCCESS)
      return result;

   extract = *(volatile uint64_t *)(output_page +
                                    CS_USER_IO_OUTPUT_CS_EXTRACT);
   if (subq->kbase.insert - extract + required > KBASE_RINGBUF_SIZE)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "kbase: CS ring did not reclaim consumed space");

   return VK_SUCCESS;
}

static VkResult
kbase_subqueue_emit_job(struct panvk_gpu_queue *queue, uint32_t subqueue,
                        uint64_t stream_addr, uint32_t stream_size,
                        uint32_t flush_id, uint64_t gpu_id,
                        const uint64_t *wait_seqnos)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct drm_panthor_csif_info *csif_info = panvk_get_csif_props(dev);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];

   VkResult result = kbase_subqueue_reserve_ring(queue, subqueue);
   if (result != VK_SUCCESS)
      return result;

   uint32_t offset = subq->kbase.insert % KBASE_RINGBUF_SIZE;

   /* If the entry would straddle the end of the ring, pad with NOPs
    * (zero-filled instructions) and restart at the beginning. */
   if (offset + KBASE_RING_JOB_MAX_SIZE > KBASE_RINGBUF_SIZE) {
      memset((uint8_t *)subq->kbase.ringbuf_cpu + offset, 0,
             KBASE_RINGBUF_SIZE - offset);
      kbase_cache_clean_range((uint8_t *)subq->kbase.ringbuf_cpu + offset,
                              KBASE_RINGBUF_SIZE - offset);
      subq->kbase.insert += KBASE_RINGBUF_SIZE - offset;
      offset = 0;
   }

   struct cs_buffer ring_buf = {
      .cpu = (uint64_t *)((uint8_t *)subq->kbase.ringbuf_cpu + offset),
      .gpu = subq->kbase.ringbuf_dev + offset,
      .capacity = KBASE_RING_JOB_MAX_SIZE / sizeof(uint64_t),
   };
   struct cs_builder_conf conf = {
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers =
         MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .ls_sb_slot = SB_ID(LS),
   };
   struct cs_builder b;

   cs_builder_init(&b, &conf, ring_buf);

   /* The kbase wrapper executes LS stores, waits, flushes and the final
    * deferred sync before/around the called PanVK stream.  The normal PanVK
    * init stream also programs these scoreboard slots, but on kbase that
    * stream is reached through this wrapper, so the wrapper has to make its
    * own async slots valid first. */
#if PAN_ARCH >= 11
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_ENDPOINT, SB_ITER(0));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_MASK_WAIT, SB_WAIT_ITER(0));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_OTHER, SB_ID(LS));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                      SB_ID(DEFERRED_SYNC));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_MASK_STREAM,
                      dev->csf.sb.all_iters_mask & ~SB_WAIT_ITER(0));
#else
   cs_set_scoreboard_entry(&b, SB_ITER(0), SB_ID(LS));
#endif

   /* kbase userspace-owned queues need their resource requirements to be
    * declared in the queue ring itself before ordinary commands are run.
    * The panthor kernel path owns that scheduling contract internally, but on
    * kbase our flush+CALL wrapper is the first firmware-visible work item. */
   cs_req_res(&b, kbase_resource_mask(subqueue));

   /* The PanVK command streams assume the subqueue context register is live
    * on entry. The panthor kernel ring preserves that queue ABI for us, but
    * kbase executes userspace-owned ring entries, so restore it at each CALL
    * boundary instead of relying on the init stream's register state to
    * survive separate kicks. */
   cs_move64_to(&b, cs_subqueue_ctx_reg(&b),
                panvk_priv_mem_dev_addr(subq->context));

   /* The ring sequence may only clobber the FW-unpreserved registers (the
    * top 4), but cs_builder_init() reserves at least 3 registers for its
    * own chunk linking — which our fixed-size ring entries never trigger —
    * and cs_reg_tuple() refuses to hand those out.  Construct the indices
    * directly instead. */
   uint32_t reg = csif_info->cs_reg_count - 4;
   struct cs_index addr64 = {
      .type = CS_INDEX_REGISTER,
      .size = 2,
      .reg = reg,
   };
   struct cs_index val32 = {
      .type = CS_INDEX_REGISTER,
      .size = 1,
      .reg = reg + 2,
   };
   struct cs_index val64 = {
      .type = CS_INDEX_REGISTER,
      .size = 2,
      .reg = reg + 2,
   };
   uint64_t seqno_addr = kbase_subqueue_seqno_dev_addr(queue, subqueue);
   uint64_t target_seqno = subq->kbase.emitted_jobs + 1;

   for (uint32_t i = 0; wait_seqnos && i < PANVK_SUBQUEUE_COUNT; i++) {
      if (!wait_seqnos[i])
         continue;
      flush_id = 0;
      if (i == subqueue)
         continue;
      cs_move64_to(&b, addr64, kbase_subqueue_seqno_dev_addr(queue, i));
      cs_move64_to(&b, val64, wait_seqnos[i] - 1);
      cs_sync64_wait(&b, false, MALI_CS_CONDITION_GREATER, val64, addr64);
   }

   /* The heap context can be renewed between synchronous submissions to
    * release chunks that proprietary kbase accounts per CSG.  Refresh the
    * firmware heap state at every graphics CALL so it observes the current
    * context address. */
   if (subqueue != PANVK_SUBQUEUE_COMPUTE) {
      cs_move64_to(&b, addr64, queue->tiler_heap.context.dev_addr);
      cs_heap_set(&b, addr64);
   }

   /* Diagnostic breadcrumbs are useful when investigating a hang, but each
    * one introduces an LS transaction and a scoreboard stall in the hot
    * submission path.  Normal operation only needs the completion writes
    * below, so make the extra instrumentation opt-in with
    * PANVK_DEBUG=kbase_diag. */
   if (PANVK_DEBUG(KBASE_DIAG)) {
      cs_move64_to(&b, addr64, seqno_addr);
      cs_move64_to(&b, val64, KBASE_SEQNO_MARK_PRE_CALL | target_seqno);
      cs_store64(&b, val64, addr64, KBASE_SEQNO_MARK_PRE_CALL_OFFSET);
      cs_wait_slot(&b, SB_ID(LS));
      cs_move32_to(&b, val32, 0);
      cs_store32(&b, val32, addr64, KBASE_SEQNO_STREAM_PROGRESS_OFFSET);
      cs_wait_slot(&b, SB_ID(LS));
   }

   if (stream_size) {
      /* Make CPU-written command-stream/descriptor memory visible to the
       * GPU before calling into it (0x233 flush, same as the panthor
       * kernel: clean+invalidate L2/LSC, invalidate other caches). */
      cs_move32_to(&b, val32, flush_id);
      cs_flush_caches(&b, MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE,
                      MALI_CS_FLUSH_MODE_CLEAN_AND_INVALIDATE,
                      MALI_CS_OTHER_FLUSH_MODE_INVALIDATE, val32,
                      cs_defer(0, SB_ID(IMM_FLUSH)));
      cs_wait_slot(&b, SB_ID(IMM_FLUSH));

      cs_move64_to(&b, addr64, stream_addr);
      cs_move32_to(&b, val32, stream_size);
      cs_call(&b, addr64, val32);
   }

   if (PANVK_DEBUG(KBASE_DIAG)) {
      cs_move64_to(&b, addr64, seqno_addr);
      cs_move64_to(&b, val64, KBASE_SEQNO_MARK_POST_CALL | target_seqno);
      cs_store64(&b, val64, addr64, KBASE_SEQNO_MARK_POST_CALL_OFFSET);
      cs_wait_slot(&b, SB_ID(LS));
   }

   /* Signal completion once all prior operations retired: an explicit WAIT
    * on all scoreboard slots followed by a SYNC_ADD64 on the cell proper
    * with an empty wait mask, matching the panthor kernel and proprietary
    * timeline path.  A deferred op's wait mask must not include its own
    * signal slot, so waiting through the defer is not an option.
    *
    * Keep the secondary LS completion copy as diagnostic instrumentation.
    * It adds an LS transaction and a scoreboard stall to every submitted
    * job, while normal completion and CQS notification already use the
    * system-scope SYNC_ADD64 below.  Nothing is emitted after that sync
    * operation (except the register-less ERROR_BARRIER), so its operands
    * cannot be clobbered while the deferred operation is in flight. */
   cs_move64_to(&b, addr64, seqno_addr);
   cs_wait_slots(&b, dev->csf.sb.all_mask);
   if (PANVK_DEBUG(KBASE_DIAG)) {
      cs_move64_to(&b, val64, KBASE_SEQNO_MARK_POST_WAIT | target_seqno);
      cs_store64(&b, val64, addr64, KBASE_SEQNO_MARK_POST_WAIT_OFFSET);
      cs_wait_slot(&b, SB_ID(LS));

      cs_move64_to(&b, val64, target_seqno);
      cs_store64(&b, val64, addr64, KBASE_SEQNO_LS_COPY_OFFSET);
      cs_wait_slot(&b, SB_ID(LS));
   }
   cs_move64_to(&b, val64, 1);
   cs_sync64_add(&b, true, MALI_CS_SYNC_SCOPE_SYSTEM, val64, addr64,
                 cs_defer(0, SB_ID(DEFERRED_SYNC)));

   /* Fault-recovery boundary, matching the panthor kernel sequence. */
   cs_error_barrier(&b);

   cs_end(&b);

   if (!cs_is_valid(&b))
      return panvk_errorf(dev, VK_ERROR_UNKNOWN,
                          "kbase: CS ring emission failed");

   uint32_t entry_size = cs_root_chunk_size(&b);
   assert(entry_size <= KBASE_RING_JOB_MAX_SIZE);

   /* Ring entries must be cacheline-aligned to please the CS prefetcher
    * (the panthor kernel pads its ring slots the same way).  The pad is
    * zero-filled, i.e. NOPs. */
   uint32_t padded_size = ALIGN_POT(entry_size, 64);
   if (padded_size != entry_size) {
      memset((uint8_t *)subq->kbase.ringbuf_cpu + offset + entry_size, 0,
             padded_size - entry_size);
   }

   subq->kbase.last_last_job_offset = subq->kbase.last_job_offset;
   subq->kbase.last_job_offset = offset;
   subq->kbase.last_job_size = padded_size;
   subq->kbase.last_job_entry_size = entry_size;
   subq->kbase.last_stream_addr = stream_addr;
   subq->kbase.last_stream_size = stream_size;
   subq->kbase.last_flush_id = flush_id;

   /* kbase queue rings follow the proprietary userspace model: CPU-written
    * ring cachelines must be explicitly cleaned before the firmware is
    * notified through USER_IO, even when the BO is not advertised as a Mesa
    * WB mapping. */
   kbase_cache_clean_range((uint8_t *)subq->kbase.ringbuf_cpu + offset,
                           padded_size);

   subq->kbase.insert += padded_size;
   subq->kbase.emitted_jobs++;

   mesa_logd("kbase: emitted subqueue %u job %" PRIu64
             ": ring offset %u, entry %u/%u bytes, stream 0x%" PRIx64
             "/%u, flush %u",
             subqueue, subq->kbase.emitted_jobs, offset, entry_size,
             padded_size, stream_addr, stream_size, flush_id);

   if (dev->debug.decode_ctx && PANVK_DEBUG(DUMP)) {
      pandecode_user_msg(dev->debug.decode_ctx,
         "\nSubqueue %d Ringbuffer Trampoline [job %lu, gpu_va: %lx, cpu=%p, Size %u bytes, to %lx]:\n",
         subqueue, subq->kbase.emitted_jobs, subq->kbase.ringbuf_dev + offset,
         subq->kbase.ringbuf_cpu + offset, padded_size, stream_addr);

      pandecode_cs_binary(dev->debug.decode_ctx,
                           subq->kbase.ringbuf_dev + offset,
                           padded_size,
                           gpu_id);
   }

   return VK_SUCCESS;
}

/* Publish the new insert offset in the USER_IO input page and kick the
 * scheduler. */
static void
kbase_subqueue_publish(struct panvk_gpu_queue *queue, uint32_t subqueue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];
   uint8_t *input_page = (uint8_t *)subq->kbase.user_io + 4096;

   /* kbase_subqueue_emit_job() has already cleaned the CPU-written ring
    * cachelines and completed that clean with kbase_gpu_wmb().  Do not issue
    * a second full-system barrier here: it cannot make those writes any more
    * visible and only serializes the CPU submission path. */

   *(volatile uint64_t *)(input_page + CS_USER_IO_INPUT_CS_INSERT) =
      subq->kbase.insert;

   /* Active queues can consume a userspace doorbell without an ioctl.  Check
    * CS_ACTIVE again after ringing it; if a suspend raced the write, the
    * scheduler kick below safely resumes the group. */
   kbase_gpu_wmb();
   uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
   volatile uint32_t *active =
      (volatile uint32_t *)(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE);

   if (*active) {
      *(volatile uint32_t *)subq->kbase.user_io = 1;
      kbase_gpu_wmb();
      if (*active)
         return;
   }

   kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
}

static VkResult
kbase_subqueue_wait_seqno(struct panvk_gpu_queue *queue, uint32_t subqueue,
                          uint64_t target_seqno, uint32_t rekick_mask,
                          bool allow_ring_drain, uint64_t abs_timeout_ns)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];
   volatile struct panvk_cs_sync64 *cell =
      kbase_subqueue_seqno_cell(queue, subqueue);
   volatile uint64_t *ls_copy =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_LS_COPY_OFFSET);
   volatile uint64_t *mark_pre_call =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_MARK_PRE_CALL_OFFSET);
   volatile uint64_t *mark_post_call =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_MARK_POST_CALL_OFFSET);
   volatile uint64_t *mark_post_wait =
      (volatile uint64_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_MARK_POST_WAIT_OFFSET);
   volatile uint32_t *stream_progress =
      (volatile uint32_t *)((volatile uint8_t *)cell +
                            KBASE_SEQNO_STREAM_PROGRESS_OFFSET);
   const uint8_t *input_page = (uint8_t *)subq->kbase.user_io + 4096;
   const uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
   uint64_t target_insert = subq->kbase.insert;
   uint64_t seqno_addr = kbase_subqueue_seqno_dev_addr(queue, subqueue);
   int64_t start = os_time_get_nano();
   int64_t last_kick = start;
   uint64_t watchdog = (uint64_t)start + KBASE_WAIT_TIMEOUT_NS;
   uint64_t deadline = MIN2(abs_timeout_ns, watchdog);
   uint32_t last_job_offset = subq->kbase.last_job_offset;
   uint32_t last_last_job_offset = subq->kbase.last_last_job_offset;
   // mesa_logd("kbase: starting subqueue %u job %" PRIu64
   //           ": seqno %" PRIu64 ", ls_copy %" PRIu64
   //           ", stream progress 0x%x",
   //           subqueue, target_seqno, (uint64_t)cell->seqno, *ls_copy,
   //           *stream_progress);

   /* CS_EXTRACT only reports how far firmware has fetched the command stream;
    * asynchronous GPU jobs issued by those commands may still be running.
    * Only accept the completion writes emitted after the all-scoreboard wait.
    */
   uint64_t prev_extract = 0xffffffffffffffffULL;
   uint64_t prev_seqno = 0xffffffffffffffffULL;
   int prev_error_type = 0xffffffff;
   while (true) {
      uint64_t insert = *(volatile uint64_t *)(input_page +
                                                CS_USER_IO_INPUT_CS_INSERT);
      uint64_t extract = *(volatile uint64_t *)(output_page +
                                                CS_USER_IO_OUTPUT_CS_EXTRACT);
      uint32_t active = *(volatile uint32_t *)(output_page +
                                               CS_USER_IO_OUTPUT_CS_ACTIVE);
      if (prev_extract != extract || prev_seqno != cell->seqno) {
         (void)active;
         prev_extract = extract;
         prev_seqno = cell->seqno;
      }
      kbase_cache_invalidate_range((const void *)cell, kbase_seqno_stride());
      if (cell->seqno >= target_seqno ||
          (PANVK_DEBUG(KBASE_DIAG) && *ls_copy >= target_seqno) ||
          (allow_ring_drain && extract >= target_insert && !active))
         break;

      if (cell->error) {
         /* CS output block mirrors the CS registers. cell->error is only
          * a fault flag; CS_FAULT (0x80) and CS_FAULT_INFO (0x88) carry
          * the exception type and the fault address. A real fault has a
          * non-zero exception type. Exception type 0 with info 0 is not
          * a GPU exception — keep waiting for the seqno. */
         uint32_t cs_fault = *(volatile uint32_t *)(output_page + 0x80);
         uint32_t cs_fault_lo = *(volatile uint32_t *)(output_page + 0x88);
         uint32_t cs_fault_hi = *(volatile uint32_t *)(output_page + 0x8c);
         uint32_t exc = cs_fault & 0xffu;
         if (exc == 0 && cs_fault_lo == 0 && cs_fault_hi == 0)
            goto kbase_wait_continue;
         mesa_loge("kbase: CS error 0x%x on subqueue %u"
                   " fault 0x%x info 0x%08x%08x",
                   cell->error, subqueue, cs_fault, cs_fault_hi, cs_fault_lo);
         return vk_queue_set_lost(&queue->vk,
                                  "kbase: CS error 0x%x on subqueue %u"
                                  " fault 0x%x info 0x%08x%08x",
                                  cell->error, subqueue, cs_fault,
                                  cs_fault_hi, cs_fault_lo);
      }

   kbase_wait_continue:

      int64_t now = os_time_get_nano();

      /* Re-kick periodically: a kick can race with an in-flight group
       * suspend and get dropped. */
      if (now - last_kick > 500ll * 1000000ll) {
         u_foreach_bit(i, rekick_mask) {
            kbase_kmod_csf_queue_kick(
               dev->kmod.dev, queue->subqueues[i].kbase.ringbuf_dev);
         }
         last_kick = now;
      }

      if ((uint64_t)now >= deadline) {
         if (deadline < watchdog)
            return VK_TIMEOUT;

         const volatile uint64_t *ring = subq->kbase.ringbuf_cpu;
         uint64_t extract_line = extract & ~(uint64_t)63;
         uint64_t last_off = subq->kbase.last_job_offset;

         /* Log directly: on queue-init failures the vk_queue_set_lost
          * message never reaches the user. */
         mesa_loge("kbase: timeout on subqueue %u: seqno %" PRIu64
                   ", ls_copy %" PRIu64 ", target %" PRIu64
                   ", marks pre/post-call/post-wait 0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64
                   ", stream progress 0x%x"
                   ", insert %" PRIu64 ", extract %" PRIu64
                   ", active %u, error 0x%x"
                   ", ring[0..3] 0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64,
                   subqueue, (uint64_t)cell->seqno, *ls_copy,
                   target_seqno, *mark_pre_call, *mark_post_call,
                   *mark_post_wait, *stream_progress, target_insert, extract,
                   active, cell->error, ring[0], ring[1], ring[2], ring[3]);
         mesa_loge("kbase: last job on subqueue %u: ring offset %u, entry "
                   "%u/%u bytes, stream 0x%" PRIx64 "/%u, flush %u, "
                   "extract offset %" PRIu64,
                   subqueue, subq->kbase.last_job_offset,
                   subq->kbase.last_job_entry_size, subq->kbase.last_job_size,
                   subq->kbase.last_stream_addr, subq->kbase.last_stream_size,
                   subq->kbase.last_flush_id, extract % KBASE_RINGBUF_SIZE);
         mesa_loge("kbase: last ring[0..15] 0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64,
                   kbase_ring_qword(subq, last_off + 0),
                   kbase_ring_qword(subq, last_off + 8),
                   kbase_ring_qword(subq, last_off + 16),
                   kbase_ring_qword(subq, last_off + 24),
                   kbase_ring_qword(subq, last_off + 32),
                   kbase_ring_qword(subq, last_off + 40),
                   kbase_ring_qword(subq, last_off + 48),
                   kbase_ring_qword(subq, last_off + 56),
                   kbase_ring_qword(subq, last_off + 64),
                   kbase_ring_qword(subq, last_off + 72),
                   kbase_ring_qword(subq, last_off + 80),
                   kbase_ring_qword(subq, last_off + 88),
                   kbase_ring_qword(subq, last_off + 96),
                   kbase_ring_qword(subq, last_off + 104),
                   kbase_ring_qword(subq, last_off + 112),
                   kbase_ring_qword(subq, last_off + 120));
         mesa_loge("kbase: extract line ring[0..7] 0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64 "/0x%" PRIx64
                   "/0x%" PRIx64 "/0x%" PRIx64,
                   kbase_ring_qword(subq, extract_line + 0),
                   kbase_ring_qword(subq, extract_line + 8),
                   kbase_ring_qword(subq, extract_line + 16),
                   kbase_ring_qword(subq, extract_line + 24),
                   kbase_ring_qword(subq, extract_line + 32),
                   kbase_ring_qword(subq, extract_line + 40),
                   kbase_ring_qword(subq, extract_line + 48),
                   kbase_ring_qword(subq, extract_line + 56));

         kbase_log_queue_syncobjs(queue);

         for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
            kbase_log_subqueue_state(queue, i, "timeout snapshot");

         if (dev->debug.decode_ctx && insert > extract) {
            // pandecode_user_msg(dev->debug.decode_ctx,
            //    "\nSubqueue %d Ringbuffer Trampoline [job %lu, gpu_va: %lx, cpu=%p, Size %u bytes, to %lx]:\n",
            //    subqueue, subq->kbase.emitted_jobs, subq->kbase.ringbuf_dev + offset,
            //    subq->kbase.ringbuf_cpu + offset, padded_size, stream_addr);
            mesa_logi("kbase: csf timeout on subqueue %d @ job %lu / %lu: insert %lu (gpu_va: %lx), extract %lu (gpu_va: %lx)"
               ", last_job_offset %u (gpu_va: %lx), last_last_job_offset %u (gpu_va: %lx)",
               subqueue, cell->seqno, target_seqno, insert, subq->kbase.ringbuf_dev + insert,
               extract, subq->kbase.ringbuf_dev + extract,
               last_job_offset, subq->kbase.ringbuf_dev + last_job_offset,
               last_last_job_offset, subq->kbase.ringbuf_dev + last_last_job_offset);
            struct panvk_physical_device *phys_dev = to_panvk_physical_device(dev->vk.physical);
            pandecode_cs_binary(dev->debug.decode_ctx,
                                 subq->kbase.ringbuf_dev + last_last_job_offset,
                                 insert - last_last_job_offset,
                                 phys_dev->kmod.dev->props.gpu_id);
         }

         return vk_queue_set_lost(&queue->vk,
                                  "kbase: timeout on subqueue %u", subqueue);
      }

      /* Block on a CSF notification (consuming one event when available)
       * instead of busy-spinning: this is what lets the kernel service the
       * submitted work — tiler-heap OOM growth, sync-update wakeups and
       * group scheduling — while we wait.  A pure spin starves that path.
       * Cap the blocking wait so the re-kick timer and overall timeout
       * still fire. */
      int64_t remaining = deadline > (uint64_t)now
                             ? MIN2(deadline - (uint64_t)now,
                                    20ull * 1000000ull)
                             : 0;
      int cqs_ret = target_seqno
                       ? kbase_kmod_csf_wait_cqs64(
                            dev->kmod.dev, seqno_addr, target_seqno - 1,
                            remaining)
                       : -1;

      if (cqs_ret < 0) {
         int error_type = kbase_kmod_csf_wait_event(dev->kmod.dev, remaining);

         if (dev->debug.decode_ctx && insert > extract && error_type) {
            // pandecode_user_msg(dev->debug.decode_ctx,
            //    "\nSubqueue %d Ringbuffer Trampoline [job %lu, gpu_va: %lx, cpu=%p, Size %u bytes, to %lx]:\n",
            //    subqueue, subq->kbase.emitted_jobs, subq->kbase.ringbuf_dev + offset,
            //    subq->kbase.ringbuf_cpu + offset, padded_size, stream_addr);
            mesa_logi("kbase: csf error/potential timeout (error=%d) on subqueue %d @ job %lu / %lu: insert %lu (gpu_va: %lx), extract %lu (gpu_va: %lx)"
               ", last_job_offset %u (gpu_va: %lx), last_last_job_offset %u (gpu_va: %lx)",
               error_type, subqueue, cell->seqno, target_seqno, insert, subq->kbase.ringbuf_dev + insert,
               extract, subq->kbase.ringbuf_dev + extract,
               last_job_offset, subq->kbase.ringbuf_dev + last_job_offset,
               last_last_job_offset, subq->kbase.ringbuf_dev + last_last_job_offset);
            struct panvk_physical_device *phys_dev = to_panvk_physical_device(dev->vk.physical);
            pandecode_cs_binary(dev->debug.decode_ctx,
                                 subq->kbase.ringbuf_dev + extract,
                                 insert - extract,
                                 phys_dev->kmod.dev->props.gpu_id);
         }
         prev_error_type = error_type;
      }
   }

   bool completed =
      cell->seqno >= target_seqno ||
      (PANVK_DEBUG(KBASE_DIAG) && *ls_copy >= target_seqno);

   mesa_logd("kbase: completed subqueue %u job %" PRIu64
             ": seqno %" PRIu64 ", ls_copy %" PRIu64
             ", stream progress 0x%x%s",
             subqueue, target_seqno, (uint64_t)cell->seqno, *ls_copy,
             *stream_progress, completed ? "" : " (ring-drain init fallback)");

   // print_stack_trace();

   return VK_SUCCESS;
}

static VkResult
kbase_queue_wait_current(struct panvk_gpu_queue *queue,
                         uint64_t abs_timeout_ns)
{
   uint32_t target_mask = 0;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (queue->subqueues[i].kbase.emitted_jobs)
         target_mask |= BITFIELD_BIT(i);
   }

   u_foreach_bit(i, target_mask) {
      VkResult result = kbase_subqueue_wait_seqno(
         queue, i, queue->subqueues[i].kbase.emitted_jobs, target_mask, false,
         abs_timeout_ns);
      if (result != VK_SUCCESS) {
         mesa_loge("%s: failed to wait for subqueue %u seqno %" PRIu64,
                   __func__, i, queue->subqueues[i].kbase.emitted_jobs);
         return result;
      }
   }

   mesa_logi("%s: wait completed successfully", __func__);
   return VK_SUCCESS;
}

static void
kbase_destroy_group(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct panvk_subqueue *subq = &queue->subqueues[i];

      if (subq->kbase.user_io)
         kbase_kmod_csf_queue_term(dev->kmod.dev, subq->kbase.ringbuf_dev,
                                   subq->kbase.user_io);

      pan_kmod_bo_put(subq->kbase.ringbuf_bo);
      subq->kbase.ringbuf_bo = NULL;
      subq->kbase.user_io = NULL;

      if (subq->kbase.group_handle != UINT32_MAX) {
         kbase_kmod_csf_group_destroy(dev->kmod.dev,
                                      subq->kbase.group_handle);
         subq->kbase.group_handle = UINT32_MAX;
      }
   }
}

static VkResult
kbase_create_group(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   VkResult result;

   queue->group_handle = UINT32_MAX;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      queue->subqueues[i].kbase.group_handle = UINT32_MAX;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      struct panvk_subqueue *subq = &queue->subqueues[i];

      if (kbase_kmod_csf_group_create(dev->kmod.dev, 1,
                                      &subq->kbase.group_handle)) {
         result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to create a kbase queue group");
         goto err_destroy_group;
      }

      subq->kbase.ringbuf_bo =
         pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, KBASE_RINGBUF_SIZE,
                           0);
      // TODO: register this mapping
      if (!subq->kbase.ringbuf_bo) {
         result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                               "Failed to allocate a kbase CS ring buffer");
         goto err_destroy_group;
      }

      subq->kbase.ringbuf_cpu = pan_kmod_bo_mmap(
         subq->kbase.ringbuf_bo, PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
      if (subq->kbase.ringbuf_cpu == MAP_FAILED) {
         subq->kbase.ringbuf_cpu = NULL;
         result = panvk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                               "Failed to map a kbase CS ring buffer");
         goto err_destroy_group;
      }

      struct pan_kmod_vm_op op = {
         .type = PAN_KMOD_VM_OP_TYPE_MAP,
         .va = {
            .start = PAN_KMOD_VM_MAP_AUTO_VA,
            .size = KBASE_RINGBUF_SIZE,
         },
         .map = {
            .bo = subq->kbase.ringbuf_bo,
            .bo_offset = 0,
         },
      };
      if (pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op,
                           1)) {
         result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                               "Failed to GPU map a kbase CS ring buffer");
         goto err_destroy_group;
      }
      subq->kbase.ringbuf_dev = op.va.start;

      if (dev->debug.decode_ctx) {
         pandecode_inject_mmap(dev->debug.decode_ctx, subq->kbase.ringbuf_dev,
                               subq->kbase.ringbuf_cpu, KBASE_RINGBUF_SIZE, "kbase_ringbuf");
      }

      subq->kbase.user_io = kbase_kmod_csf_queue_bind(
         dev->kmod.dev, subq->kbase.group_handle, 0,
         subq->kbase.ringbuf_dev,
         KBASE_RINGBUF_SIZE);
      if (!subq->kbase.user_io) {
         result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to bind a kbase CS queue");
         goto err_destroy_group;
      }

      subq->kbase.insert = 0;
      subq->kbase.emitted_jobs = 0;
      mesa_logd("kbase: bound subqueue %u to group %u CSI0, ring CPU %p, "
                "ring VA 0x%" PRIx64 ", user_io %p",
                i, subq->kbase.group_handle, subq->kbase.ringbuf_cpu,
                subq->kbase.ringbuf_dev,
                subq->kbase.user_io);
   }

   return VK_SUCCESS;

err_destroy_group:
   kbase_destroy_group(queue);
   return result;
}

#endif /* HAVE_PAN_KMOD_KBASE */

static void
finish_render_desc_ringbuf(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const bool tracing_enabled = PANVK_DEBUG(TRACE);
   struct panvk_desc_ringbuf *ringbuf = &queue->render_desc_ringbuf;

   panvk_pool_free_mem(&ringbuf->syncobj);

   if (dev->debug.decode_ctx && ringbuf->addr.dev) {
      pandecode_inject_free(dev->debug.decode_ctx, ringbuf->addr.dev,
                            ringbuf->size);
      if (!tracing_enabled)
         pandecode_inject_free(dev->debug.decode_ctx,
                               ringbuf->addr.dev + ringbuf->size,
                               ringbuf->size);
   }

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      /* The BO's own mapping goes away with the BO; only the alias region
       * (not created in tracing mode) needs explicit teardown. */
      if (ringbuf->addr.dev && !tracing_enabled)
         kbase_kmod_alias_destroy(dev->kmod.dev, ringbuf->addr.dev,
                                  ringbuf->size, 2);
   } else
#endif
   if (ringbuf->addr.dev) {
      panvk_address_binding_report(dev, NULL, ringbuf->addr.dev, ringbuf->size,
                                   VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

      struct pan_kmod_vm_op op = {
         .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
         .va = {
            .start = ringbuf->addr.dev,
            .size = ringbuf->size * (tracing_enabled ? 2 : 1),
         },
      };

      ASSERTED int ret =
         pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
      assert(!ret);

      panvk_as_free(dev, ringbuf->addr.dev, ringbuf->size * 2);
   }

   if (ringbuf->addr.host) {
      ASSERTED int ret =
         pan_kmod_bo_munmap(ringbuf->bo, ringbuf->addr.host, ringbuf->size);
      assert(!ret);
   }

   pan_kmod_bo_put(ringbuf->bo);
}

static VkResult
init_render_desc_ringbuf(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const bool tracing_enabled = PANVK_DEBUG(TRACE);
   uint32_t flags = panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_NO_MMAP);
   struct panvk_desc_ringbuf *ringbuf = &queue->render_desc_ringbuf;
   uint64_t dev_addr = 0;
   int ret;

   if (tracing_enabled) {
      ringbuf->size = debug_get_num_option("PANVK_DESC_TRACEBUF_SIZE",
                                           DEFAULT_DESC_TRACEBUF_SIZE);
      flags |= PAN_KMOD_BO_FLAG_GPU_UNCACHED;
      assert(ringbuf->size > MIN_DESC_TRACEBUF_SIZE &&
             util_is_power_of_two_nonzero(ringbuf->size));
   } else {
      ringbuf->size = RENDER_DESC_RINGBUF_SIZE;
   }

   ringbuf->bo =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, ringbuf->size, flags);
   if (!ringbuf->bo)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create a descriptor ring buffer context");

   if (!(flags & PAN_KMOD_BO_FLAG_NO_MMAP)) {
      ringbuf->addr.host = pan_kmod_bo_mmap(ringbuf->bo, PROT_READ | PROT_WRITE,
                                            MAP_SHARED, NULL);
      if (ringbuf->addr.host == MAP_FAILED)
         return panvk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                             "Failed to CPU map ringbuf BO");
   }

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      /* kbase assigns the BO VA itself; report it back through an AUTO_VA
       * map op. */
      struct pan_kmod_vm_op map_op = {
         .type = PAN_KMOD_VM_OP_TYPE_MAP,
         .va = {
            .start = PAN_KMOD_VM_MAP_AUTO_VA,
            .size = ringbuf->size,
         },
         .map = {
            .bo = ringbuf->bo,
            .bo_offset = 0,
         },
      };
      ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE,
                             &map_op, 1);
      if (ret)
         return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                             "Failed to GPU map ringbuf BO");

      if (tracing_enabled) {
         /* No wraparound mirror needed (and no guard page support). */
         ringbuf->addr.dev = map_op.va.start;
      } else {
         /* Mapping one BO twice back-to-back at a chosen address is not
          * possible on kbase; use a MEM_ALIAS region instead.  The helper
          * guarantees the mapping never crosses a 4G boundary, so the
          * wraparound can be encoded with 32-bit operations. */
         ringbuf->addr.dev = kbase_kmod_alias_create(
            dev->kmod.dev, map_op.va.start, ringbuf->size, 2);
         if (!ringbuf->addr.dev)
            return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                                "Failed to GPU map ringbuf BO (alias)");
      }
      goto ringbuf_mapped;
   }
#endif

   /* We choose the alignment to guarantee that we won't ever cross a 4G
    * boundary when accessing the mapping. This way we can encode the wraparound
    * using 32-bit operations. */
   dev_addr = panvk_as_alloc(dev, PANVK_NO_EXEC_VA_HEAP, ringbuf->size * 2,
                             ringbuf->size * 2);

   if (!dev_addr)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to allocate virtual address for ringbuf BO");

   struct pan_kmod_vm_op vm_ops[] = {
      {
         .type = PAN_KMOD_VM_OP_TYPE_MAP,
         .va = {
            .start = dev_addr,
            .size = ringbuf->size,
         },
         .map = {
            .bo = ringbuf->bo,
            .bo_offset = 0,
         },
      },
      {
         .type = PAN_KMOD_VM_OP_TYPE_MAP,
         .va = {
            .start = dev_addr + ringbuf->size,
            .size = ringbuf->size,
         },
         .map = {
            .bo = ringbuf->bo,
            .bo_offset = 0,
         },
      },
   };

   /* If tracing is enabled, we keep the second part of the mapping unmapped
    * to serve as a guard region. */
   ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, vm_ops,
                          tracing_enabled ? 1 : ARRAY_SIZE(vm_ops));
   if (ret) {
      panvk_as_free(dev, dev_addr, ringbuf->size * 2);
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to GPU map ringbuf BO");
   }

   ringbuf->addr.dev = dev_addr;

#ifdef HAVE_PAN_KMOD_KBASE
ringbuf_mapped:
#endif
   panvk_address_binding_report(dev, NULL, ringbuf->addr.dev, ringbuf->size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   if (dev->debug.decode_ctx) {
      pandecode_inject_mmap(dev->debug.decode_ctx, ringbuf->addr.dev,
                            ringbuf->addr.host, ringbuf->size, NULL);
      if (!tracing_enabled)
         pandecode_inject_mmap(dev->debug.decode_ctx,
                               ringbuf->addr.dev + ringbuf->size,
                               ringbuf->addr.host, ringbuf->size, NULL);
   }

   struct panvk_pool_alloc_info alloc_info = {
      .size = sizeof(struct panvk_cs_sync32),
      .alignment = 64,
   };

   ringbuf->syncobj = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_check_alloc(ringbuf->syncobj))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create the render desc ringbuf context");

   panvk_priv_mem_write(ringbuf->syncobj, 0, struct panvk_cs_sync32, syncobj) {
      *syncobj = (struct panvk_cs_sync32){
         .seqno = RENDER_DESC_RINGBUF_SIZE,
      };
   }
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev))
      kbase_clean_priv_mem(ringbuf->syncobj, 0,
                           sizeof(struct panvk_cs_sync32));
#endif

   return VK_SUCCESS;
}

static void
finish_subqueue_tracing(struct panvk_gpu_queue *queue,
                        enum panvk_subqueue_id subqueue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];

   if (subq->tracebuf.addr.dev) {
      uint64_t pgsize = panvk_get_gpu_page_size(dev);

      panvk_address_binding_report(dev, NULL, subq->tracebuf.addr.dev,
                                   subq->tracebuf.size,
                                   VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

      pandecode_inject_free(dev->debug.decode_ctx, subq->tracebuf.addr.dev,
                            subq->tracebuf.size);

      struct pan_kmod_vm_op op = {
         .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
         .va = {
            .start = subq->tracebuf.addr.dev,
            .size = subq->tracebuf.size,
         },
      };

      ASSERTED int ret =
         pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &op, 1);
      assert(!ret);

      panvk_as_free(dev, subq->tracebuf.addr.dev, subq->tracebuf.size + pgsize);
   }

   if (subq->tracebuf.addr.host) {
      ASSERTED int ret =
         pan_kmod_bo_munmap(subq->tracebuf.bo, subq->tracebuf.addr.host,
                            subq->tracebuf.size);
      assert(!ret);
   }

   pan_kmod_bo_put(subq->tracebuf.bo);

   vk_free(&dev->vk.alloc, subq->reg_file);
}

#ifdef HAVE_PAN_KMOD_KBASE
static VkResult
kbase_submit_init_subqueues(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_physical_device *phys_dev = to_panvk_physical_device(dev->vk.physical);
   uint32_t touched = 0;

   for (uint32_t subqueue = 0; subqueue < PANVK_SUBQUEUE_COUNT; subqueue++) {
      struct panvk_subqueue *subq = &queue->subqueues[subqueue];

      if (!subq->kbase.init_pending)
         continue;

      VkResult res =
         kbase_subqueue_emit_job(queue, subqueue, subq->kbase.init_stream_addr,
                                 subq->kbase.init_stream_size,
                                 subq->kbase.init_flush_id,
                                 phys_dev->kmod.dev->props.gpu_id, NULL);
      if (res != VK_SUCCESS)
         return panvk_errorf(dev->vk.physical, VK_ERROR_INITIALIZATION_FAILED,
                             "Failed to initialize subqueue");

      subq->kbase.init_pending = 0;
      touched |= BITFIELD_BIT(subqueue);
   }

   u_foreach_bit(i, touched)
      kbase_subqueue_publish(queue, i);

   u_foreach_bit(i, touched) {
      VkResult res = kbase_subqueue_wait_seqno(
         queue, i, queue->subqueues[i].kbase.emitted_jobs, touched, false,
         UINT64_MAX);
      if (res != VK_SUCCESS)
         return panvk_errorf(dev->vk.physical, VK_ERROR_INITIALIZATION_FAILED,
                             "Failed to initialize subqueue");
   }

   return VK_SUCCESS;
}
#endif

static VkResult
init_subqueue_tracing(struct panvk_gpu_queue *queue,
                      enum panvk_subqueue_id subqueue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];
   uint64_t dev_addr;

   if (!PANVK_DEBUG(TRACE))
      return VK_SUCCESS;

   subq->reg_file =
      vk_zalloc(&dev->vk.alloc, sizeof(uint32_t) * 256, sizeof(uint64_t),
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!subq->reg_file)
      return panvk_errorf(dev->vk.physical, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "Failed to allocate reg file cache");

   subq->tracebuf.size = debug_get_num_option("PANVK_CS_TRACEBUF_SIZE",
                                              DEFAULT_CS_TRACEBUF_SIZE);
   assert(subq->tracebuf.size > MIN_CS_TRACEBUF_SIZE &&
          util_is_power_of_two_nonzero(subq->tracebuf.size));

   subq->tracebuf.bo =
      pan_kmod_bo_alloc(dev->kmod.dev, dev->kmod.vm, subq->tracebuf.size,
                        PAN_KMOD_BO_FLAG_GPU_UNCACHED);
   if (!subq->tracebuf.bo)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create a CS tracebuf");

   subq->tracebuf.addr.host = pan_kmod_bo_mmap(
      subq->tracebuf.bo, PROT_READ | PROT_WRITE, MAP_SHARED, NULL);
   if (subq->tracebuf.addr.host == MAP_FAILED) {
      subq->tracebuf.addr.host = NULL;
      return panvk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "Failed to CPU map tracebuf");
   }

   /* Add a guard page. */
   uint64_t pgsize = panvk_get_gpu_page_size(dev);
   dev_addr = panvk_as_alloc(dev, PANVK_NO_EXEC_VA_HEAP,
                             subq->tracebuf.size + pgsize, pgsize);

   if (!dev_addr)
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to allocate virtual address for tracebuf");

   struct pan_kmod_vm_op vm_op = {
      .type = PAN_KMOD_VM_OP_TYPE_MAP,
      .va = {
         .start = dev_addr,
         .size = subq->tracebuf.size,
      },
      .map = {
         .bo = subq->tracebuf.bo,
         .bo_offset = 0,
      },
   };

   /* If tracing is enabled, we keep the second part of the mapping unmapped
    * to serve as a guard region. */
   int ret =
      pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &vm_op, 1);
   if (ret) {
      panvk_as_free(dev, dev_addr, subq->tracebuf.size + pgsize);
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to GPU map ringbuf BO");
   }

   subq->tracebuf.addr.dev = dev_addr;

   panvk_address_binding_report(dev, NULL, subq->tracebuf.addr.dev,
                                subq->tracebuf.size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   if (dev->debug.decode_ctx) {
      pandecode_inject_mmap(dev->debug.decode_ctx, subq->tracebuf.addr.dev,
                            subq->tracebuf.addr.host, subq->tracebuf.size,
                            NULL);
   }

   return VK_SUCCESS;
}

static void
finish_subqueue(struct panvk_gpu_queue *queue, enum panvk_subqueue_id subqueue)
{
   panvk_pool_free_mem(&queue->subqueues[subqueue].context);
   panvk_pool_free_mem(&queue->subqueues[subqueue].req_resource.buf);
   panvk_pool_free_mem(&queue->subqueues[subqueue].regs_save);
#ifdef HAVE_PAN_KMOD_KBASE
   panvk_pool_free_mem(&queue->subqueues[subqueue].kbase.init_cs);
#endif
   finish_subqueue_tracing(queue, subqueue);
}

static VkResult
init_utrace(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   VkResult result;

   const struct vk_sync_type *sync_type = phys_dev->sync_types[0];

#ifdef HAVE_PAN_KMOD_KBASE
   /* kbase queues use the synchronous submission model and never process
    * utrace on the GPU timeline, so no utrace sync object is needed. */
   if (gpu_queue_uses_kbase(dev))
      return VK_SUCCESS;
#endif

   /* A DRM-backed timeline sync is required for CSF queue operation. */
   if (!sync_type || !vk_sync_type_is_drm_syncobj(sync_type) ||
       !(sync_type->features & VK_SYNC_FEATURE_TIMELINE)) {
      return vk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                       "panvk CSF: timeline DRM syncobj required for queue "
                       "creation");
   }

   result = vk_sync_create(&dev->vk, sync_type, VK_SYNC_IS_TIMELINE, 0,
                           &queue->utrace.sync);
   if (result != VK_SUCCESS)
      return result;

   queue->utrace.next_value = 1;

   return VK_SUCCESS;
}

static uint32_t
get_resource_mask(enum panvk_subqueue_id subqueue)
{
   switch (subqueue) {
   case PANVK_SUBQUEUE_VERTEX_TILER:
      return CS_IDVS_RES | CS_TILER_RES;
   case PANVK_SUBQUEUE_FRAGMENT:
      return CS_FRAG_RES;
   case PANVK_SUBQUEUE_COMPUTE:
      return CS_COMPUTE_RES;
   default:
      UNREACHABLE("Unknown subqueue");
   }
}

static VkResult
init_subqueue(struct panvk_gpu_queue *queue, enum panvk_subqueue_id subqueue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_subqueue *subq = &queue->subqueues[subqueue];
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(queue->vk.base.device->physical);

   VkResult result = init_subqueue_tracing(queue, subqueue);
   if (result != VK_SUCCESS)
      return result;

   struct panvk_pool_alloc_info alloc_info;

   if (dev->dump_region_size[subqueue]) {
      alloc_info.size = dev->dump_region_size[subqueue];
      alloc_info.alignment = sizeof(uint32_t);
      subq->regs_save = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
      if (!panvk_priv_mem_check_alloc(subq->regs_save)) {
         return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                             "Failed to allocate register save area");
      }
   }

   /* When tracing is enabled, we want to use a non-cached pool, so can get
    * up-to-date context even if the CS crashed in the middle. */
   struct panvk_pool *mempool =
      PANVK_DEBUG(TRACE) ? &dev->mempools.rw_nc : &dev->mempools.rw;

   alloc_info.size = sizeof(uint64_t);
   alloc_info.alignment = 64;
   subq->req_resource.buf = panvk_pool_alloc_mem(mempool, alloc_info);
   if (!panvk_priv_mem_check_alloc(subq->req_resource.buf))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create a req_resource buffer");

   struct cs_builder b;
   const struct drm_panthor_csif_info *csif_info =
      panvk_get_csif_props(dev);

   struct cs_buffer root_cs = {
      .cpu = panvk_priv_mem_host_addr(subq->req_resource.buf),
      .gpu = panvk_priv_mem_dev_addr(subq->req_resource.buf),
      .capacity = 1,
   };
   struct cs_builder_conf conf = {
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .ls_sb_slot = SB_ID(LS),
   };

   cs_builder_init(&b, &conf, root_cs);
   cs_req_res(&b, get_resource_mask(subqueue));
   cs_end(&b);
   assert(cs_is_valid(&b));
   subq->req_resource.cs_buffer_size = cs_root_chunk_size(&b);
   subq->req_resource.cs_buffer_addr = cs_root_chunk_gpu_addr(&b);
   cs_builder_fini(&b);
   panvk_priv_mem_flush(subq->req_resource.buf, 0,
                        subq->req_resource.cs_buffer_size);

   alloc_info.size = sizeof(struct panvk_cs_subqueue_context);
   alloc_info.alignment = 64;

   subq->context = panvk_pool_alloc_mem(mempool, alloc_info);
   if (!panvk_priv_mem_check_alloc(subq->context))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to create a queue context");

   panvk_priv_mem_write(subq->context, 0, struct panvk_cs_subqueue_context,
                        cs_ctx) {
      *cs_ctx = (struct panvk_cs_subqueue_context){
         .syncobjs = panvk_priv_mem_dev_addr(queue->syncobjs),
         .debug.tracebuf.cs = subq->tracebuf.addr.dev,
#if PAN_ARCH == 10
         /* On the VT/COMPUTE queue, the first iter_sb will skipped since
          * cs_next_iter_sb() is called before the first use, but that's okay,
          * because the next slot will be equally free, and the skipped one will
          * be re-used at some point.
          * On the fragment queue, we increment the iterator when the
          * FINISH_FRAGMENT job is issued, which is why we need this value
          * to point to a valid+free scoreboard from the start.
          */
         .iter_sb = SB_ITER(0),
#endif
         .reg_dump_addr = panvk_priv_mem_dev_addr(subq->regs_save),
      };

#ifdef HAVE_PAN_KMOD_KBASE
      if (gpu_queue_uses_kbase(dev) && PANVK_DEBUG(KBASE_DIAG)) {
         cs_ctx->debug.kbase_progress_addr =
            kbase_subqueue_seqno_dev_addr(queue, subqueue) +
            KBASE_SEQNO_STREAM_PROGRESS_OFFSET;
      }
#endif

      if (subqueue != PANVK_SUBQUEUE_COMPUTE) {
         cs_ctx->render.tiler_heap =
            panvk_priv_mem_dev_addr(queue->tiler_heap.desc);
         /* Our geometry buffer comes 4k after the tiler heap, and we encode the
          * size in the lower 12 bits so the address can be copied directly
          * to the tiler descriptors. */
         cs_ctx->render.geom_buf =
            (cs_ctx->render.tiler_heap + 4096) | ((64 * 1024) >> 12);

         /* Initialize the ringbuf */
         cs_ctx->render.desc_ringbuf = (struct panvk_cs_desc_ringbuf){
            .syncobj =
               panvk_priv_mem_dev_addr(queue->render_desc_ringbuf.syncobj),
            .ptr = queue->render_desc_ringbuf.addr.dev,
            .pos = 0,
         };
      }

      if (subqueue == PANVK_SUBQUEUE_FRAGMENT) {
         /* The tiler OOM exception handler is registered to the fragment
          * queue, so the scratch FBD buffer is only needed there. We leave
          * it to NULL on other queues to make sure any attempt to access it
          * results in a NULL deref that can be caught.
          */
         cs_ctx->tiler_oom_ctx.ir_scratch_fbd_ptr =
            panvk_priv_mem_dev_addr(queue->tiler_heap.oom_fbd);
      }
   }
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev))
      kbase_clean_priv_mem(subq->context, 0,
                           sizeof(struct panvk_cs_subqueue_context));
#endif

   /* We use the geometry buffer for our temporary CS buffer. */
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      alloc_info.size = 4096;
      alloc_info.alignment = 64;
      subq->kbase.init_cs = panvk_pool_alloc_mem(mempool, alloc_info);
      if (!panvk_priv_mem_check_alloc(subq->kbase.init_cs))
         return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                             "Failed to create a kbase queue init CS buffer");

      root_cs = (struct cs_buffer){
         .cpu = panvk_priv_mem_host_addr(subq->kbase.init_cs),
         .gpu = panvk_priv_mem_dev_addr(subq->kbase.init_cs),
         .capacity = 4096 / sizeof(uint64_t),
      };
   } else
#endif
   {
      root_cs = (struct cs_buffer){
         .cpu = panvk_priv_mem_host_addr(queue->tiler_heap.desc) + 4096,
         .gpu = panvk_priv_mem_dev_addr(queue->tiler_heap.desc) + 4096,
         .capacity = 64 * 1024 / sizeof(uint64_t),
      };
   }
   conf = (struct cs_builder_conf){
      .nr_registers = csif_info->cs_reg_count,
      .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
      .ls_sb_slot = SB_ID(LS),
   };

   assert(panvk_priv_mem_dev_addr(queue->tiler_heap.desc) != 0);

   cs_builder_init(&b, &conf, root_cs);
   /* Pass the context. */
   cs_move64_to(&b, cs_subqueue_ctx_reg(&b),
                panvk_priv_mem_dev_addr(subq->context));

   /* Intialize scoreboard slots used for asynchronous operations. */
#if PAN_ARCH >= 11
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_ENDPOINT, SB_ITER(0));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_MASK_WAIT, SB_WAIT_ITER(0));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_OTHER, SB_ID(LS));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                      SB_ID(DEFERRED_SYNC));
   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_MASK_STREAM,
                      dev->csf.sb.all_iters_mask & ~SB_WAIT_ITER(0));
#else
   cs_set_scoreboard_entry(&b, SB_ITER(0), SB_ID(LS));
#endif

   /* We do greater than test on sync objects, and given the reference seqno
    * registers are all zero at init time, we need to initialize all syncobjs
    * with a seqno of one. */
   panvk_priv_mem_write(queue->syncobjs,
                        subqueue * sizeof(struct panvk_cs_sync64),
                        struct panvk_cs_sync64, syncobj) {
      syncobj->seqno = 1;
   }
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      kbase_clean_priv_mem(queue->syncobjs,
                           subqueue * sizeof(struct panvk_cs_sync64),
                           sizeof(struct panvk_cs_sync64));
   }
#endif

   if (subqueue != PANVK_SUBQUEUE_COMPUTE) {
      struct cs_index heap_ctx_addr = cs_scratch_reg64(&b, 0);

      /* Pre-set the heap context on the vertex-tiler/fragment queues. */
      cs_move64_to(&b, heap_ctx_addr, queue->tiler_heap.context.dev_addr);
      cs_heap_set(&b, heap_ctx_addr);
   }
   cs_end(&b);

   assert(cs_is_valid(&b));

   uint32_t init_stream_size = cs_root_chunk_size(&b);
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      panvk_priv_mem_flush(subq->kbase.init_cs, 0, init_stream_size);
      kbase_cache_clean_range(root_cs.cpu, init_stream_size);
   } else
#endif
   {
      panvk_priv_mem_flush(queue->tiler_heap.desc, 4096, init_stream_size);
   }

   struct drm_panthor_sync_op syncop = {
      .flags =
         DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ | DRM_PANTHOR_SYNC_OP_SIGNAL,
      .handle = queue->syncobj_handle,
      .timeline_value = 0,
   };
   struct drm_panthor_queue_submit qsubmit = {
      .queue_index = subqueue,
      .stream_size = cs_root_chunk_size(&b),
      .stream_addr = cs_root_chunk_gpu_addr(&b),
      .latest_flush = panvk_get_flush_id(dev),
      .syncs = DRM_PANTHOR_OBJ_ARRAY(1, &syncop),
   };
   struct drm_panthor_group_submit gsubmit = {
      .group_handle = queue->group_handle,
      .queue_submits = DRM_PANTHOR_OBJ_ARRAY(1, &qsubmit),
   };

   cs_builder_fini(&b);

   pan_kmod_flush_bo_map_syncs(dev->kmod.dev);

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      subq->kbase.init_stream_addr = qsubmit.stream_addr;
      subq->kbase.init_stream_size = qsubmit.stream_size;
      subq->kbase.init_flush_id = qsubmit.latest_flush;
      subq->kbase.init_pending = 1;
   } else
#endif
   {
      int ret = pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANTHOR_GROUP_SUBMIT,
                               &gsubmit);
      if (ret)
         return panvk_errorf(dev->vk.physical, VK_ERROR_INITIALIZATION_FAILED,
                             "Failed to initialized subqueue: %m");

      ret = pan_kmod_sync_wait(dev->kmod.dev, &queue->syncobj_handle, 1,
                               INT64_MAX, 0, NULL);
      if (ret)
         return panvk_errorf(dev->vk.physical, VK_ERROR_INITIALIZATION_FAILED,
                             "SyncobjWait failed: %m");

      pan_kmod_sync_reset(dev->kmod.dev, &queue->syncobj_handle, 1);
   }

   if (PANVK_DEBUG(TRACE)) {
      pandecode_user_msg(dev->debug.decode_ctx, "Init subqueue %d binary\n\n",
                         subqueue);
      pandecode_cs_binary(dev->debug.decode_ctx, qsubmit.stream_addr,
                          qsubmit.stream_size,
                          phys_dev->kmod.dev->props.gpu_id);
   }

   return VK_SUCCESS;
}

static void
cleanup_queue(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      finish_subqueue(queue, i);

   if (queue->utrace.sync)
      vk_sync_destroy(&dev->vk, queue->utrace.sync);

   finish_render_desc_ringbuf(queue);

   panvk_pool_free_mem(&queue->syncobjs);
#ifdef HAVE_PAN_KMOD_KBASE
   pan_kmod_bo_put(queue->kbase_seqnos.bo);
   queue->kbase_seqnos.bo = NULL;
#endif
}

static VkResult
init_queue(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   VkResult result;

   struct panvk_pool_alloc_info alloc_info = {
      .size =
         ALIGN_POT(sizeof(struct panvk_cs_sync64), 64) * PANVK_SUBQUEUE_COUNT,
      .alignment = 64,
   };

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      struct panvk_priv_bo *sync_bo;
      result = panvk_priv_bo_create(dev, alloc_info.size,
                                    PAN_KMOD_BO_FLAG_CSF_EVENT,
                                    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
                                    &sync_bo);
      if (result != VK_SUCCESS)
         return result;

      queue->syncobjs = (struct panvk_priv_mem){
         .bo = (uintptr_t)sync_bo,
         .offset = 0,
      };
   } else
#endif
   {
      queue->syncobjs = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   }
   if (!panvk_priv_mem_check_alloc(queue->syncobjs))
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to allocate subqueue sync objects");

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      result = kbase_init_seqnos(queue);
      if (result != VK_SUCCESS)
         return result;
   }
#endif

   result = init_render_desc_ringbuf(queue);
   if (result != VK_SUCCESS)
      goto err_cleanup_queue;

   result = init_utrace(queue);
   if (result != VK_SUCCESS)
      goto err_cleanup_queue;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      result = init_subqueue(queue, i);
      if (result != VK_SUCCESS)
         goto err_cleanup_queue;
   }

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      result = kbase_submit_init_subqueues(queue);
      if (result != VK_SUCCESS)
         goto err_cleanup_queue;
   }
#endif

   if (PANVK_DEBUG(TRACE))
      pandecode_next_frame(dev->debug.decode_ctx);

   return VK_SUCCESS;

err_cleanup_queue:
   cleanup_queue(queue);
   return result;
}

static VkResult
create_group(struct panvk_gpu_queue *queue,
             enum drm_panthor_group_priority group_priority,
             uint32_t shader_core_count)
{
   const struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(queue->vk.base.device->physical);

   struct drm_panthor_queue_create qc[] = {
      [PANVK_SUBQUEUE_VERTEX_TILER] =
         {
            .priority = 1,
            .ringbuf_size = 64 * 1024,
         },
      [PANVK_SUBQUEUE_FRAGMENT] =
         {
            .priority = 1,
            .ringbuf_size = 64 * 1024,
         },
      [PANVK_SUBQUEUE_COMPUTE] =
         {
            .priority = 1,
            .ringbuf_size = 64 * 1024,
         },
   };

   uint8_t max_compute_cores = util_bitcount64(phys_dev->compute_core_mask);
   uint8_t max_fragment_cores = util_bitcount64(phys_dev->fragment_core_mask);

   if (shader_core_count) {
      max_compute_cores = MIN2(shader_core_count, max_compute_cores);
      max_fragment_cores = MIN2(shader_core_count, max_fragment_cores);
   }

   struct drm_panthor_group_create gc = {
      .compute_core_mask = phys_dev->compute_core_mask,
      .fragment_core_mask = phys_dev->fragment_core_mask,
      .tiler_core_mask = 1,
      .max_compute_cores = max_compute_cores,
      .max_fragment_cores = max_fragment_cores,
      .max_tiler_cores = 1,
      .priority = group_priority,
      .queues = DRM_PANTHOR_OBJ_ARRAY(ARRAY_SIZE(qc), qc),
      .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
   };

   int ret = pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANTHOR_GROUP_CREATE, &gc);
   if (ret)
      return panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                          "Failed to create a scheduling group");

   queue->group_handle = gc.group_handle;
   return VK_SUCCESS;
}

static void
destroy_group(struct panvk_gpu_queue *queue)
{
   const struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct drm_panthor_group_destroy gd = {
      .group_handle = queue->group_handle,
   };

   ASSERTED int ret =
      pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANTHOR_GROUP_DESTROY, &gd);
   assert(!ret);
}

static VkResult
init_tiler(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_tiler_heap *tiler_heap = &queue->tiler_heap;
   VkResult result;

   /* We allocate the tiler heap descriptor and geometry buffer in one go,
    * so we can pass it through a single 64-bit register to the VERTEX_TILER
    * command streams. */
   struct panvk_pool_alloc_info alloc_info = {
      .size = (64 * 1024) + 8192,
      .alignment = 4096,
   };

   tiler_heap->desc = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_check_alloc(tiler_heap->desc)) {
      result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Failed to create a tiler heap context");
      goto err_free_desc;
   }

   tiler_heap->chunk_size = phys_dev->csf.tiler.chunk_size;

   alloc_info.size = get_fbd_size(true, MAX_RTS);
#if PAN_ARCH >= 14
   const unsigned fbds_alignment = alignof(struct panvk_fb_layer_state);
#else
   const unsigned fbds_alignment = pan_alignment(FRAMEBUFFER);
#endif
   alloc_info.alignment = fbds_alignment;
   tiler_heap->oom_fbd = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_check_alloc(tiler_heap->oom_fbd)) {
      result = panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Failed to create a scratch FBD");
      goto err_free_desc;
   }

   uint64_t first_heap_chunk;

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      uint64_t heap_ctx_va, first_chunk_va;
      /* KBASE_IOCTL_CS_TILER_HEAP_INIT.group_id selects a physical memory
       * group.  Keep it on the default group, matching panfork. */
      const uint32_t tiler_heap_mem_group = 0;
      /* Match the Valhall G610/G710 kbase reference.  The panthor-derived
       * limit of 64 can strand large geometry workloads on the tiler iterator;
       * larger multi-GiB limits can trigger Android's global OOM killer. */
      const uint32_t tiler_heap_max_chunks =
         MAX2(phys_dev->csf.tiler.max_chunks, 200);

      if (kbase_kmod_csf_tiler_heap_create(
             dev->kmod.dev, tiler_heap->chunk_size,
             phys_dev->csf.tiler.initial_chunks, tiler_heap_max_chunks,
             65535, tiler_heap_mem_group, &heap_ctx_va, &first_chunk_va)) {
         result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to create a tiler heap context");
         goto err_free_desc;
      }

      tiler_heap->context.handle = 0;
      tiler_heap->context.dev_addr = heap_ctx_va;
      first_heap_chunk = first_chunk_va;
   } else
#endif
   {
      struct drm_panthor_tiler_heap_create thc = {
         .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
         .chunk_size = tiler_heap->chunk_size,
         .initial_chunk_count = phys_dev->csf.tiler.initial_chunks,
         .max_chunks = phys_dev->csf.tiler.max_chunks,
         .target_in_flight = 65535,
      };

      int ret = pan_kmod_ioctl(dev->drm_fd,
                               DRM_IOCTL_PANTHOR_TILER_HEAP_CREATE, &thc);
      if (ret) {
         result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to create a tiler heap context");
         goto err_free_desc;
      }

      tiler_heap->context.handle = thc.handle;
      tiler_heap->context.dev_addr = thc.tiler_heap_ctx_gpu_va;
      first_heap_chunk = thc.first_heap_chunk_gpu_va;
   }

   panvk_priv_mem_write_desc(tiler_heap->desc, 0, TILER_HEAP, cfg) {
      cfg.size = tiler_heap->chunk_size;
      cfg.base = first_heap_chunk;
      cfg.bottom = cfg.base + 64;
      cfg.top = cfg.base + cfg.size;
   }

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      kbase_clean_priv_mem(tiler_heap->desc, 0, pan_size(TILER_HEAP));
      mesa_logd("kbase: tiler heap desc 0x%" PRIx64
                ": base 0x%" PRIx64 ", bottom 0x%" PRIx64
                ", top 0x%" PRIx64 ", geom 0x%" PRIx64
                ", oom_fbd 0x%" PRIx64,
                panvk_priv_mem_dev_addr(tiler_heap->desc), first_heap_chunk,
                first_heap_chunk + 64,
                first_heap_chunk + tiler_heap->chunk_size,
                panvk_priv_mem_dev_addr(tiler_heap->desc) + 4096,
                panvk_priv_mem_dev_addr(tiler_heap->oom_fbd));
   }
#endif

   return VK_SUCCESS;

err_free_desc:
   panvk_pool_free_mem(&tiler_heap->desc);
   panvk_pool_free_mem(&tiler_heap->oom_fbd);
   return result;
}

static void
cleanup_tiler(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_tiler_heap *tiler_heap = &queue->tiler_heap;

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      /* The queue groups are terminated by now, so the retired context (if
       * any) is no longer firmware-visible and can be destroyed with the
       * current one. */
      if (queue->kbase_retired_heap.ctx) {
         kbase_kmod_csf_tiler_heap_destroy(dev->kmod.dev,
                                           queue->kbase_retired_heap.ctx);
         queue->kbase_retired_heap.ctx = 0;
      }
      kbase_kmod_csf_tiler_heap_destroy(dev->kmod.dev,
                                        tiler_heap->context.dev_addr);
   } else
#endif
   {
      struct drm_panthor_tiler_heap_destroy thd = {
         .handle = tiler_heap->context.handle,
      };
      ASSERTED int ret = pan_kmod_ioctl(
         dev->drm_fd, DRM_IOCTL_PANTHOR_TILER_HEAP_DESTROY, &thd);
      assert(!ret);
   }

   panvk_pool_free_mem(&tiler_heap->desc);
   panvk_pool_free_mem(&tiler_heap->oom_fbd);
}

#ifdef HAVE_PAN_KMOD_KBASE
/* Destroy the heap context retired by the previous renewal, provided every
 * graphics subqueue has executed a ring entry emitted after the retirement.
 * Each ring entry re-issues HEAP_SET before its CALL, and the caller's
 * pre-renewal drain guarantees those entries completed, so the firmware
 * provably no longer holds a reference to the retired context.  Destroying
 * it any earlier lets kbase free (and the custom-VA allocator reuse) its
 * chunks while a CS HEAP_SET register can still point at them: the firmware
 * then walks whatever now lives at the old chunk VAs as a chunk list and
 * faults on a garbage pointer (observed as an exception 0xc0 CSG fatal at a
 * wild sideband address during heavy allocation churn).  Returns false when
 * the context must stay retired for now. */
static bool
kbase_try_destroy_retired_heap(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   if (!queue->kbase_retired_heap.ctx)
      return true;

   if (queue->subqueues[PANVK_SUBQUEUE_VERTEX_TILER].kbase.emitted_jobs <=
          queue->kbase_retired_heap.vt_jobs ||
       queue->subqueues[PANVK_SUBQUEUE_FRAGMENT].kbase.emitted_jobs <=
          queue->kbase_retired_heap.frag_jobs)
      return false;

   kbase_kmod_csf_tiler_heap_destroy(dev->kmod.dev,
                                     queue->kbase_retired_heap.ctx);
   queue->kbase_retired_heap.ctx = 0;
   return true;
}

static VkResult
kbase_renew_tiler_heap(struct panvk_gpu_queue *queue)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_tiler_heap *tiler_heap = &queue->tiler_heap;
   const uint32_t max_chunks = MAX2(phys_dev->csf.tiler.max_chunks, 200);
   uint64_t new_ctx, first_chunk;

   /* Hold at most one retired context: if the previous one is still
    * firmware-visible (no graphics ring entry ran since), skip this
    * renewal and retry on a later graphics submission. */
   if (!kbase_try_destroy_retired_heap(queue))
      return VK_SUCCESS;

   if (kbase_kmod_csf_tiler_heap_create(
          dev->kmod.dev, tiler_heap->chunk_size,
          phys_dev->csf.tiler.initial_chunks, max_chunks, 65535, 0,
          &new_ctx, &first_chunk)) {
      return panvk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Failed to renew the kbase tiler heap");
   }

   uint64_t old_ctx = tiler_heap->context.dev_addr;
   tiler_heap->context.dev_addr = new_ctx;

   panvk_priv_mem_write_desc(tiler_heap->desc, 0, TILER_HEAP, cfg) {
      cfg.size = tiler_heap->chunk_size;
      cfg.base = first_chunk;
      cfg.bottom = cfg.base + 64;
      cfg.top = cfg.base + cfg.size;
   }
   kbase_clean_priv_mem(tiler_heap->desc, 0, pan_size(TILER_HEAP));

   queue->kbase_retired_heap.ctx = old_ctx;
   queue->kbase_retired_heap.vt_jobs =
      queue->subqueues[PANVK_SUBQUEUE_VERTEX_TILER].kbase.emitted_jobs;
   queue->kbase_retired_heap.frag_jobs =
      queue->subqueues[PANVK_SUBQUEUE_FRAGMENT].kbase.emitted_jobs;
   return VK_SUCCESS;
}
#endif

struct panvk_queue_submit {
   const struct panvk_physical_device *phys_dev;
   struct panvk_device *dev;
   struct panvk_gpu_queue *queue;

   bool process_utrace;
   bool force_sync;

   uint32_t qsubmit_count;
   uint32_t wait_queue_mask;
   uint32_t signal_queue_mask;
   uint32_t req_resource_subqueue_mask;
   uint64_t tiler_work_estimate;

   struct drm_panthor_queue_submit *qsubmits;
   struct drm_panthor_sync_op *wait_ops;
   struct drm_panthor_sync_op *signal_ops;

#ifdef HAVE_PAN_KMOD_KBASE
   uint64_t kbase_target_seqnos[PANVK_SUBQUEUE_COUNT];
#endif

   struct {
      uint32_t queue_mask;
      enum panvk_subqueue_id first_subqueue;
      enum panvk_subqueue_id last_subqueue;
      bool needs_clone;
      const struct u_trace *last_ut;
      struct panvk_utrace_flush_data *data_storage;

      struct panvk_utrace_flush_data *data[PANVK_SUBQUEUE_COUNT];
   } utrace;
};

struct panvk_queue_submit_stack_storage {
   struct drm_panthor_queue_submit qsubmits[8];
   struct drm_panthor_sync_op syncops[8];
};

static void
panvk_queue_submit_init(struct panvk_queue_submit *submit,
                        struct vk_queue *vk_queue)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);
   struct vk_device *vk_dev = vk_queue->base.device;

   *submit = (struct panvk_queue_submit){
      .phys_dev = to_panvk_physical_device(vk_dev->physical),
      .dev = to_panvk_device(vk_dev),
      .queue = container_of(vk_queue, struct panvk_gpu_queue, vk),
   };

   submit->process_utrace =
      u_trace_should_process(&submit->dev->utrace.utctx) &&
      submit->phys_dev->kmod.dev->props.timestamp_frequency;
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(submit->dev))
      submit->process_utrace = false;
#endif

   submit->force_sync = PANVK_DEBUG(TRACE) || PANVK_DEBUG(SYNC);
}

static VkResult
panvk_queue_submit_init_storage(
   struct panvk_queue_submit *submit, const struct vk_queue_submit *vk_submit,
   struct panvk_queue_submit_stack_storage *stack_storage)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);
   submit->utrace.first_subqueue = PANVK_SUBQUEUE_COUNT;
   VkPipelineStageFlags2 cmd_stage_mask = VK_PIPELINE_STAGE_2_NONE;
   for (uint32_t i = 0; i < vk_submit->command_buffer_count; i++) {
      struct panvk_cmd_buffer *cmdbuf = container_of(
         vk_submit->command_buffers[i], struct panvk_cmd_buffer, vk);

      if (UINT64_MAX - submit->tiler_work_estimate <
          cmdbuf->state.tiler_work_estimate)
         submit->tiler_work_estimate = UINT64_MAX;
      else
         submit->tiler_work_estimate += cmdbuf->state.tiler_work_estimate;

      for (uint32_t j = 0; j < ARRAY_SIZE(cmdbuf->state.cs); j++) {
         struct cs_builder *b = panvk_get_cs_builder(cmdbuf, j);
         assert(cs_is_valid(b));
         if (cs_is_empty(b))
            continue;

         cmd_stage_mask |= panvk_get_subqueue_stages(j);
         submit->qsubmit_count++;

         struct panvk_subqueue *subq = &submit->queue->subqueues[j];
         /* If we need a resource the subqueue has not requested yet. */
         if (b->req_resource_mask & (~subq->req_resource.mask)) {
            /* Ensure we do not need a resource not expected for this subqueue. */
            assert(!(b->req_resource_mask & (~get_resource_mask(j))));
            submit->qsubmit_count++;
            submit->req_resource_subqueue_mask |= BITFIELD_BIT(j);
            subq->req_resource.mask = get_resource_mask(j);
         }

         struct u_trace *ut = &cmdbuf->utrace.uts[j];
         if (submit->process_utrace && u_trace_has_points(ut)) {
            submit->utrace.queue_mask |= BITFIELD_BIT(j);
            if (submit->utrace.first_subqueue == PANVK_SUBQUEUE_COUNT)
               submit->utrace.first_subqueue = j;
            submit->utrace.last_subqueue = j;
            submit->utrace.last_ut = ut;

            if (!(cmdbuf->flags &
                  VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) {
               /* we will follow the user cs with a timestamp copy cs */
               submit->qsubmit_count++;
               submit->utrace.needs_clone = true;
            }
         }
      }
   }

   /* wait_stages_mask is pipeline stages which limit
    * the second synchronization scope of a semaphore wait operation */
   VkPipelineStageFlags2 wait_stages_mask = cmd_stage_mask;
   for (uint32_t i = 0; i < vk_submit->wait_count; i++) {
      wait_stages_mask |= vk_submit->waits[i].stage_mask;
   }

   /* signal_stages_mask is pipeline stages which limit
    * the first synchronization scope of a semaphore signal operation */
   VkPipelineStageFlags2 signal_stages_mask = cmd_stage_mask;
   for (uint32_t i = 0; i < vk_submit->signal_count; i++) {
      signal_stages_mask |= vk_submit->signals[i].stage_mask;
   }

   /* if there is no cs in any subqueue */
   if (cmd_stage_mask == VK_PIPELINE_STAGE_2_NONE) {
      /* signal stage mask is TOP_OF_PIPE/NONE, signal immediately */
      if (signal_stages_mask == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT ||
          signal_stages_mask == VK_PIPELINE_STAGE_2_NONE) {
         signal_stages_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      }

      /* wait stage mask is BOTTOM_OF_PIPE/NONE, wait deferred */
      if (wait_stages_mask == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT ||
          wait_stages_mask == VK_PIPELINE_STAGE_2_NONE) {
         wait_stages_mask = panvk_get_subqueue_stages(PANVK_SUBQUEUE_FRAGMENT) |
                            panvk_get_subqueue_stages(PANVK_SUBQUEUE_COMPUTE);
      }
   }

   submit->wait_queue_mask =
      vk_stages_to_subqueue_mask(wait_stages_mask, SYNC_SCOPE_SECOND);

   submit->signal_queue_mask =
      vk_stages_to_subqueue_mask(signal_stages_mask, SYNC_SCOPE_FIRST) |
      submit->utrace.queue_mask;

   /* Signal all subqueues if force_sync */
   if (submit->force_sync) {
      submit->signal_queue_mask |= BITFIELD_MASK(PANVK_SUBQUEUE_COUNT);
   }

   uint32_t syncop_count = 0;

   /* We add sync-only queue submits to place our wait/signal operations. */
   submit->qsubmit_count += util_bitcount(submit->wait_queue_mask);
   syncop_count += vk_submit->wait_count;

   submit->qsubmit_count += util_bitcount(submit->signal_queue_mask);
   syncop_count += util_bitcount(submit->signal_queue_mask);

   submit->qsubmits =
      submit->qsubmit_count <= ARRAY_SIZE(stack_storage->qsubmits)
         ? stack_storage->qsubmits
         : malloc(sizeof(*submit->qsubmits) * submit->qsubmit_count);
   if (!submit->qsubmits)
      return panvk_error(submit->dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   submit->wait_ops = syncop_count <= ARRAY_SIZE(stack_storage->syncops)
                         ? stack_storage->syncops
                         : malloc(sizeof(*submit->wait_ops) * syncop_count);
   if (!submit->wait_ops)
      return panvk_error(submit->dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   submit->signal_ops = submit->wait_ops + vk_submit->wait_count;

   /* reset so that we can initialize submit->qsubmits incrementally */
   submit->qsubmit_count = 0;

   if (submit->utrace.queue_mask) {
      submit->utrace.data_storage =
         malloc(sizeof(*submit->utrace.data_storage) *
                util_bitcount(submit->utrace.queue_mask));
      if (!submit->utrace.data_storage)
         return panvk_error(submit->dev, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   return VK_SUCCESS;
}

static void
panvk_queue_submit_cleanup_storage(
   struct panvk_queue_submit *submit,
   const struct panvk_queue_submit_stack_storage *stack_storage)
{
   if (submit->qsubmits != stack_storage->qsubmits)
      free(submit->qsubmits);
   if (submit->wait_ops != stack_storage->syncops)
      free(submit->wait_ops);

   /* either no utrace flush data or the data has been transferred to u_trace */
   assert(!submit->utrace.data_storage);
}

static void
panvk_queue_submit_init_utrace(struct panvk_queue_submit *submit,
                               const struct vk_queue_submit *vk_submit)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);

   if (!submit->utrace.queue_mask)
      return;

   /* u_trace_context processes trace events in order.  We want to make sure
    * it waits for the timestamp writes before processing the first event and
    * it can free the flush data after processing the last event.
    */
   struct panvk_utrace_flush_data *next = submit->utrace.data_storage;
   submit->utrace.data[submit->utrace.last_subqueue] = next++;
   submit->utrace.data[submit->utrace.last_subqueue]->free_self = true;

   u_foreach_bit(i, submit->utrace.queue_mask) {
      if (i != submit->utrace.last_subqueue)
         submit->utrace.data[i] = next++;

      const bool wait = i == submit->utrace.first_subqueue;
      *submit->utrace.data[i] = (struct panvk_utrace_flush_data){
         .subqueue = i,
         .sync = wait ? submit->queue->utrace.sync : NULL,
         .wait_value = wait ? submit->queue->utrace.next_value : 0,
         .free_self = false,
      };
   }
}

static void
panvk_queue_submit_init_req_resource(struct panvk_queue_submit *submit)
{
   if (!submit->req_resource_subqueue_mask)
      return;

   struct panvk_device *dev = submit->dev;
   uint32_t flush_id = panvk_get_flush_id(dev);

   u_foreach_bit(i, submit->req_resource_subqueue_mask) {
      struct panvk_subqueue *subq = &submit->queue->subqueues[i];
      submit->qsubmits[submit->qsubmit_count++] =
         (struct drm_panthor_queue_submit){
            .queue_index = i,
            .stream_size = subq->req_resource.cs_buffer_size,
            .stream_addr = subq->req_resource.cs_buffer_addr,
            .latest_flush = flush_id,
         };
   }
}

static void
panvk_queue_submit_init_waits(struct panvk_queue_submit *submit,
                              const struct vk_queue_submit *vk_submit)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);
   if (!submit->wait_queue_mask)
      return;

#ifdef HAVE_PAN_KMOD_KBASE
   /* No DRM syncobjs on kbase: semaphore waits are resolved on the CPU
    * right before submission instead. */
   if (gpu_queue_uses_kbase(submit->dev))
      return;
#endif

   for (uint32_t i = 0; i < vk_submit->wait_count; i++) {
      const struct vk_sync_wait *wait = &vk_submit->waits[i];
      const struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(wait->sync);
      assert(syncobj);

      submit->wait_ops[i] = (struct drm_panthor_sync_op){
         .flags = (syncobj->base.flags & VK_SYNC_IS_TIMELINE
                      ? DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
                      : DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ) |
                  DRM_PANTHOR_SYNC_OP_WAIT,
         .handle = syncobj->syncobj,
         .timeline_value = wait->wait_value,
      };
   }

   u_foreach_bit(i, submit->wait_queue_mask) {
      submit->qsubmits[submit->qsubmit_count++] =
         (struct drm_panthor_queue_submit){
            .queue_index = i,
            .syncs =
               DRM_PANTHOR_OBJ_ARRAY(vk_submit->wait_count, submit->wait_ops),
         };
   }
}

static void
panvk_queue_submit_init_cmdbufs(struct panvk_queue_submit *submit,
                                const struct vk_queue_submit *vk_submit)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);
   struct panvk_device *dev = submit->dev;

   for (uint32_t i = 0; i < vk_submit->command_buffer_count; i++) {
      struct panvk_cmd_buffer *cmdbuf = container_of(
         vk_submit->command_buffers[i], struct panvk_cmd_buffer, vk);

      uint32_t flush_id = panvk_get_flush_id(dev);

      for (uint32_t j = 0; j < ARRAY_SIZE(cmdbuf->state.cs); j++) {
         struct cs_builder *b = panvk_get_cs_builder(cmdbuf, j);
         if (cs_is_empty(b))
            continue;

#ifdef HAVE_PAN_KMOD_KBASE
         // if (gpu_queue_uses_kbase(dev) && PANVK_DEBUG(KBASE_DIAG)) {
         //    kbase_log_stream_prefix(j, cs_root_chunk_gpu_addr(b),
         //                            cs_root_chunk_size(b),
         //                            b->root_chunk.buffer.cpu);
         // }
         // Use csf decoding instead
#endif

         submit->qsubmits[submit->qsubmit_count++] =
            (struct drm_panthor_queue_submit){
               .queue_index = j,
               .stream_size = cs_root_chunk_size(b),
               .stream_addr = cs_root_chunk_gpu_addr(b),
               .latest_flush = flush_id,
            };
      }

      if (util_bitcount(submit->utrace.queue_mask) > 0)
         flush_id = panvk_get_flush_id(dev);

      u_foreach_bit(j, submit->utrace.queue_mask) {
         struct u_trace *ut = &cmdbuf->utrace.uts[j];

         if (!u_trace_has_points(ut))
            continue;

         /* The last subqueue frees the flush data itself. */
         bool free_data = ut == submit->utrace.last_ut;

         struct u_trace clone_ut;
         if (!(cmdbuf->flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)) {
            u_trace_init(&clone_ut, &dev->utrace.utctx);
            /* For every sq, the cs buffer needs to be freed. */
            free_data = true;

            /* The clone CS builder allocates all of its chunks (including the
             * root) from the utrace copy heap via alloc_clone_cs_buffer(). */
            struct panvk_utrace_clone_cs_ctx clone_ctx = {
               .dev = dev,
            };
            util_dynarray_init(&clone_ctx.cs_bufs, NULL);
            struct cs_builder clone_builder;
            panvk_per_arch(utrace_clone_init_builder)(&clone_builder, dev,
                                                      &clone_ctx);

            u_trace_clone_append(
               u_trace_begin_iterator(ut), u_trace_end_iterator(ut), &clone_ut,
               &clone_builder, panvk_per_arch(utrace_copy_buffer));

            panvk_per_arch(utrace_clone_finish_builder)(&clone_builder);

            submit->utrace.data[j]->clone_cs_bufs = clone_ctx.cs_bufs;

            /* A mid-build overflow allocation failure leaves the builder
             * invalid; flush the original instead of submitting a broken CS. */
            if (!cs_is_valid(&clone_builder)) {
               mesa_loge("utrace: clone CS builder invalid (allocation failed "
                         "mid-build); dropping trace for this submit");
               u_trace_fini(&clone_ut);
            } else {
               submit->qsubmits[submit->qsubmit_count++] =
                  (struct drm_panthor_queue_submit){
                     .queue_index = j,
                     .stream_size = cs_root_chunk_size(&clone_builder),
                     .stream_addr = cs_root_chunk_gpu_addr(&clone_builder),
                     .latest_flush = flush_id,
                  };

               ut = &clone_ut;
            }
         }

         u_trace_flush(ut, submit->utrace.data[j], dev->vk.current_frame,
                       free_data);
      }
   }

   /* we've transferred the data ownership to utrace, if any */
   submit->utrace.data_storage = NULL;
}

static void
panvk_queue_submit_init_signals(struct panvk_queue_submit *submit,
                                const struct vk_queue_submit *vk_submit)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);
   struct panvk_gpu_queue *queue = submit->queue;

#ifdef HAVE_PAN_KMOD_KBASE
   /* No DRM syncobjs on kbase: semaphore signals are resolved on the CPU
    * after the synchronous wait instead. */
   if (gpu_queue_uses_kbase(submit->dev))
      return;
#endif

   uint32_t signal_op = 0;
   u_foreach_bit(i, submit->signal_queue_mask) {
      submit->signal_ops[signal_op] = (struct drm_panthor_sync_op){
         .flags = DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ |
                  DRM_PANTHOR_SYNC_OP_SIGNAL,
         .handle = queue->syncobj_handle,
         .timeline_value = signal_op + 1,
      };

      submit->qsubmits[submit->qsubmit_count++] =
         (struct drm_panthor_queue_submit){
            .queue_index = i,
            .syncs = DRM_PANTHOR_OBJ_ARRAY(1, &submit->signal_ops[signal_op++]),
         };
   }
}

#ifdef HAVE_PAN_KMOD_KBASE
/* Off by default: G615 query_pool.statistics_query CTS loses the device on
 * subqueue 0 with GPU waits (1-3 of 17769), never with CPU waits. */
DEBUG_GET_ONCE_BOOL_OPTION(kbase_gpu_semaphore_waits,
                           "PANVK_KBASE_GPU_SEMAPHORE_WAITS", false)

static bool
kbase_gpu_waits_disabled(void)
{
   return !debug_get_option_kbase_gpu_semaphore_waits();
}

static VkResult
kbase_wait_sync_targets(
   void *data, const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
   uint64_t abs_timeout_ns)
{
   struct panvk_gpu_queue *queue = data;
   uint32_t target_mask = 0;

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (targets[i])
         target_mask |= BITFIELD_BIT(i);
   }

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (!targets[i])
         continue;

      VkResult result = kbase_subqueue_wait_seqno(
         queue, i, targets[i], target_mask, false, abs_timeout_ns);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
kbase_wait_graphics_targets(
   struct panvk_gpu_queue *queue,
   const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
   uint64_t abs_timeout_ns)
{
   const uint32_t graphics_mask =
      BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER) |
      BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT);

   u_foreach_bit(i, graphics_mask) {
      if (!targets[i])
         continue;

      VkResult result = kbase_subqueue_wait_seqno(
         queue, i, targets[i], graphics_mask, false, abs_timeout_ns);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

/* Incoming CPU syncs are resolved before emission.  The new work itself is
 * only published here; completion is represented by the seqno snapshot and
 * consumed later by fence/semaphore waits. */
static VkResult
panvk_queue_submit_ioctl_kbase(struct panvk_queue_submit *submit,
                               const struct vk_queue_submit *vk_submit)
{
   struct panvk_device *dev = submit->dev;
   struct panvk_gpu_queue *queue = submit->queue;
   VkResult result;

   // struct pandecode_context *decode_ctx = submit->dev->debug.decode_ctx;
   // const struct pan_kmod_dev_props *props =
   //    &submit->phys_dev->kmod.dev->props;
   // if (decode_ctx) {
   //    for (uint32_t i = 0; i < submit->qsubmit_count; i++) {
   //       const struct drm_panthor_queue_submit *qsubmit = &submit->qsubmits[i];
   //       if (!qsubmit->stream_size)
   //          continue;

   //       pandecode_user_msg(decode_ctx, "CS %d on subqueue %d binaries\n\n", i,
   //                         qsubmit->queue_index);
   //       pandecode_cs_binary(decode_ctx, qsubmit->stream_addr,
   //                         qsubmit->stream_size, props->gpu_id);
   //       pandecode_user_msg(decode_ctx, "\n");
   //    }
   // } else {
   //    mesa_loge("%s: decode context not available, skipping CS binary dump",
   //              __func__);
   // }

   for (uint32_t w = 0; w < vk_submit->wait_count; w++) {
      const struct vk_sync_wait *wait = &vk_submit->waits[w];
      uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT];
      void *owner;

      if (!kbase_gpu_waits_disabled() && wait->wait_value == 0 &&
          panvk_kbase_sync_peek(wait->sync, &owner, targets) &&
          (owner == NULL || owner == queue)) {
         for (uint32_t i = 0; owner && i < PANVK_SUBQUEUE_COUNT; i++) {
            if (targets[i] <= kbase_subqueue_seqno_cell(queue, i)->seqno)
               continue;
            for (uint32_t j = 0; j < PANVK_SUBQUEUE_COUNT; j++)
               queue->kbase_gpu_waits[j][i] =
                  MAX2(queue->kbase_gpu_waits[j][i], targets[i]);
         }
         continue;
      }

      result = vk_sync_wait(&dev->vk, wait->sync, wait->wait_value,
                            VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
      if (result != VK_SUCCESS)
         return result;
   }

   /* Flush pending synchronization requests before submitting the job, to
    * make sure things are GPU-visible. */
   pan_kmod_flush_bo_map_syncs(dev->kmod.dev);

   uint32_t touched = 0;
   for (uint32_t i = 0; i < submit->qsubmit_count; i++) {
      const struct drm_panthor_queue_submit *qsubmit = &submit->qsubmits[i];

      if (!qsubmit->stream_size)
         continue;

      result = kbase_subqueue_emit_job(queue, qsubmit->queue_index,
                                       qsubmit->stream_addr,
                                       qsubmit->stream_size,
                                       qsubmit->latest_flush,
                                       submit->phys_dev->kmod.dev->props.gpu_id,
                                       queue->kbase_gpu_waits[qsubmit->queue_index]);
      if (result != VK_SUCCESS)
         return vk_queue_set_lost(&queue->vk, "kbase: ring emission failed");

      memset(queue->kbase_gpu_waits[qsubmit->queue_index], 0,
             sizeof(queue->kbase_gpu_waits[0]));

      touched |= BITFIELD_BIT(qsubmit->queue_index);
   }

   u_foreach_bit(i, touched)
      kbase_subqueue_publish(queue, i);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      submit->kbase_target_seqnos[i] = queue->subqueues[i].kbase.emitted_jobs;

   if (submit->force_sync) {
      result = kbase_wait_sync_targets(queue, submit->kbase_target_seqnos,
                                       UINT64_MAX);
      if (result != VK_SUCCESS)
         return result;
   }

   const uint32_t graphics_mask =
      BITFIELD_BIT(PANVK_SUBQUEUE_VERTEX_TILER) |
      BITFIELD_BIT(PANVK_SUBQUEUE_FRAGMENT);
   /* Clear-only fragment submissions don't use the tiler heap.  Counting
    * those towards renewal forces a graphics drain and heap replacement with
    * no memory to reclaim.  All draw paths, including indirect and secondary
    * command buffers, contribute a non-zero tiler work estimate. */
   if ((touched & graphics_mask) && submit->tiler_work_estimate) {
      queue->kbase_tiler_submit_count++;
      if (UINT64_MAX - queue->kbase_tiler_work_count <
          submit->tiler_work_estimate)
         queue->kbase_tiler_work_count = UINT64_MAX;
      else
         queue->kbase_tiler_work_count += submit->tiler_work_estimate;
   }

   uint64_t renew_work = kbase_tiler_heap_renew_work();
   if (submit->tiler_work_estimate &&
       (queue->kbase_tiler_submit_count >=
           kbase_tiler_heap_renew_interval() ||
        (renew_work && queue->kbase_tiler_work_count >= renew_work))) {
      /* Heap generations are referenced only by graphics streams.  Preserve
       * overlap with unrelated compute work instead of draining every
       * subqueue before replacing the tiler heap. */
      result = kbase_wait_graphics_targets(
         queue, submit->kbase_target_seqnos, UINT64_MAX);
      if (result != VK_SUCCESS)
         return result;

      result = kbase_renew_tiler_heap(queue);
      if (result != VK_SUCCESS)
         return vk_queue_set_lost(&queue->vk,
                                  "kbase: tiler heap renewal failed");

      queue->kbase_tiler_submit_count = 0;
      queue->kbase_tiler_work_count = 0;
   }

   return VK_SUCCESS;
}

static void
panvk_queue_submit_process_signals_kbase(struct panvk_queue_submit *submit,
                                         const struct vk_queue_submit *vk_submit)
{
   for (uint32_t i = 0; i < vk_submit->signal_count; i++) {
      const struct vk_sync_signal *signal = &vk_submit->signals[i];
      assert(signal->signal_value == 0);
      panvk_kbase_sync_set_pending(signal->sync, submit->queue,
                                   kbase_wait_sync_targets,
                                   submit->kbase_target_seqnos);
   }
}
#endif /* HAVE_PAN_KMOD_KBASE */

static VkResult
panvk_queue_submit_ioctl(struct panvk_queue_submit *submit)
{
   const struct panvk_device *dev = submit->dev;
   struct panvk_gpu_queue *queue = submit->queue;
   int ret;

   if (PANVK_DEBUG(TRACE)) {
      /* If we're tracing, we need to reset the desc ringbufs and the CS
       * tracebuf. */
      for (uint32_t i = 0; i < ARRAY_SIZE(queue->subqueues); i++) {
         panvk_priv_mem_rmw(queue->subqueues[i].context, 0,
                            struct panvk_cs_subqueue_context, ctx) {
            if (ctx->render.desc_ringbuf.ptr) {
               ctx->render.desc_ringbuf.ptr =
                  queue->render_desc_ringbuf.addr.dev;
               ctx->render.desc_ringbuf.pos = 0;
            }

            if (ctx->debug.tracebuf.cs)
               ctx->debug.tracebuf.cs = queue->subqueues[i].tracebuf.addr.dev;
         }
      }
   }

   /* Flush pending synchronization requests before submitting the job, to
    * make sure things are GPU-visible. */
   pan_kmod_flush_bo_map_syncs(dev->kmod.dev);

   struct drm_panthor_group_submit gsubmit = {
      .group_handle = queue->group_handle,
      .queue_submits =
         DRM_PANTHOR_OBJ_ARRAY(submit->qsubmit_count, submit->qsubmits),
   };

   ret = pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANTHOR_GROUP_SUBMIT, &gsubmit);
   if (ret)
      return vk_queue_set_lost(&queue->vk, "GROUP_SUBMIT: %m");

   return VK_SUCCESS;
}

static void
panvk_queue_submit_process_signals(struct panvk_queue_submit *submit,
                                   const struct vk_queue_submit *vk_submit)
{
   struct panvk_device *dev = submit->dev;
   struct panvk_gpu_queue *queue = submit->queue;
   ASSERTED int ret;

   if (!submit->signal_queue_mask)
      return;

   if (submit->force_sync) {
      uint64_t point = util_bitcount(submit->signal_queue_mask);
      ret = pan_kmod_sync_timeline_wait(dev->kmod.dev, &queue->syncobj_handle,
                                        &point, 1, INT64_MAX,
                                        DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
      assert(!ret);
   }

   for (uint32_t i = 0; i < vk_submit->signal_count; i++) {
      const struct vk_sync_signal *signal = &vk_submit->signals[i];
      const struct vk_drm_syncobj *syncobj =
         vk_sync_as_drm_syncobj(signal->sync);
      assert(syncobj);

      pan_kmod_sync_transfer(dev->kmod.dev, syncobj->syncobj,
                             signal->signal_value, queue->syncobj_handle, 0, 0);
   }

   if (submit->utrace.queue_mask) {
      const struct vk_drm_syncobj *syncobj =
         vk_sync_as_drm_syncobj(queue->utrace.sync);

      pan_kmod_sync_transfer(dev->kmod.dev, syncobj->syncobj,
                             queue->utrace.next_value++, queue->syncobj_handle,
                             0, 0);

      /* process flushed events after the syncobj is set up */
      u_trace_context_process(&dev->utrace.utctx, false);
   }

   pan_kmod_sync_reset(dev->kmod.dev, &queue->syncobj_handle, 1);
}

static void
panvk_queue_submit_process_debug(const struct panvk_queue_submit *submit,
                                 const struct vk_queue_submit *vk_submit)
{
   struct panvk_gpu_queue *queue = submit->queue;
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct pandecode_context *decode_ctx = submit->dev->debug.decode_ctx;

   if (PANVK_DEBUG(TRACE)) {
      const struct pan_kmod_dev_props *props =
         &submit->phys_dev->kmod.dev->props;

      /* First we invalidate all desc buffers to make sure we see GPU updates
       * on those. */
      for (uint32_t i = 0; i < vk_submit->command_buffer_count; i++) {
         struct panvk_cmd_buffer *cmdbuf = container_of(
            vk_submit->command_buffers[i], struct panvk_cmd_buffer, vk);

         panvk_pool_invalidate_maps(&cmdbuf->desc_pool);
      }

      pan_kmod_flush_bo_map_syncs(dev->kmod.dev);

      for (uint32_t i = 0; i < submit->qsubmit_count; i++) {
         const struct drm_panthor_queue_submit *qsubmit = &submit->qsubmits[i];
         if (!qsubmit->stream_size)
            continue;

         pandecode_user_msg(decode_ctx, "CS %d on subqueue %d binaries\n\n", i,
                            qsubmit->queue_index);
         pandecode_cs_binary(decode_ctx, qsubmit->stream_addr,
                             qsubmit->stream_size, props->gpu_id);
         pandecode_user_msg(decode_ctx, "\n");
      }

      for (uint32_t i = 0; i < ARRAY_SIZE(queue->subqueues); i++) {
         panvk_priv_mem_readback(queue->subqueues[i].context, 0,
                                 struct panvk_cs_subqueue_context, ctx) {
            size_t trace_size =
               ctx->debug.tracebuf.cs - queue->subqueues[i].tracebuf.addr.dev;

            if (trace_size) {
               assert(
                  trace_size <= queue->subqueues[i].tracebuf.size ||
                  !"OOB access on the CS tracebuf, pass a bigger PANVK_CS_TRACEBUF_SIZE");

               assert(
                  !ctx->render.desc_ringbuf.ptr ||
                  ctx->render.desc_ringbuf.pos <=
                     queue->render_desc_ringbuf.size ||
                  !"OOB access on the desc tracebuf, pass a bigger PANVK_DESC_TRACEBUF_SIZE");

               uint64_t trace = queue->subqueues[i].tracebuf.addr.dev;

               pandecode_user_msg(decode_ctx, "\nCS traces on subqueue %d\n\n",
                                  i);
               pandecode_cs_trace(decode_ctx, trace, trace_size, props->gpu_id);
               pandecode_user_msg(decode_ctx, "\n");
            }
         }
      }
   }

   if (PANVK_DEBUG(DUMP))
      pandecode_dump_mappings(decode_ctx);

   if (PANVK_DEBUG(TRACE))
      pandecode_next_frame(decode_ctx);

   /* validate last after the command streams are dumped */
   if (submit->force_sync)
      panvk_per_arch(gpu_queue_check_status)(&queue->vk);
}

VkResult
panvk_per_arch(gpu_queue_submit)(struct vk_queue *vk_queue, struct vk_queue_submit *vk_submit)
{
   PAN_TRACE_FUNC(PAN_TRACE_VK_CSF);
   struct panvk_queue_submit_stack_storage stack_storage;
   struct panvk_queue_submit submit;
   VkResult result = VK_SUCCESS;

   if (vk_queue_is_lost(vk_queue))
      return VK_ERROR_DEVICE_LOST;

   if (vk_queue_submit_has_bind(vk_submit)) {
      struct panvk_gpu_queue *queue = container_of(vk_queue, struct panvk_gpu_queue, vk);
      return panvk_queue_vm_bind(vk_queue, vk_submit, queue->syncobj_handle);
   }

   panvk_queue_submit_init(&submit, vk_queue);
   result = panvk_queue_submit_init_storage(&submit, vk_submit, &stack_storage);
   if (result != VK_SUCCESS)
      goto out;
   panvk_queue_submit_init_utrace(&submit, vk_submit);
   panvk_queue_submit_init_req_resource(&submit);
   panvk_queue_submit_init_waits(&submit, vk_submit);
   panvk_queue_submit_init_cmdbufs(&submit, vk_submit);
   panvk_queue_submit_init_signals(&submit, vk_submit);

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(submit.dev)) {
      result = panvk_queue_submit_ioctl_kbase(&submit, vk_submit);
      if (result != VK_SUCCESS)
         goto out;

      panvk_queue_submit_process_signals_kbase(&submit, vk_submit);
      panvk_queue_submit_process_debug(&submit, vk_submit);
      goto out;
   }
#endif

   result = panvk_queue_submit_ioctl(&submit);
   if (result != VK_SUCCESS)
      goto out;

   panvk_queue_submit_process_signals(&submit, vk_submit);
   panvk_queue_submit_process_debug(&submit, vk_submit);

out:
   panvk_queue_submit_cleanup_storage(&submit, &stack_storage);
   return result;
}

static enum drm_panthor_group_priority
get_panthor_group_priority(const VkDeviceQueueCreateInfo *create_info)
{
   const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(create_info->pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   const VkQueueGlobalPriorityKHR priority =
      priority_info ? priority_info->globalPriority
                    : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;

   switch (priority) {
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:
      return PANTHOR_GROUP_PRIORITY_LOW;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:
      return PANTHOR_GROUP_PRIORITY_MEDIUM;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:
      return PANTHOR_GROUP_PRIORITY_HIGH;
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR:
      return PANTHOR_GROUP_PRIORITY_REALTIME;
   default:
      UNREACHABLE("Invalid global priority");
   }
}

VkResult
panvk_per_arch(create_gpu_queue)(struct panvk_device *dev,
                                 const VkDeviceQueueCreateInfo *create_info,
                                 uint32_t queue_idx,
                                 struct vk_queue **out_queue)
{
   struct panvk_gpu_queue *queue = vk_zalloc(&dev->vk.alloc, sizeof(*queue), 8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queue)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_queue_init(&queue->vk, &dev->vk, create_info, queue_idx);
   if (result != VK_SUCCESS)
      goto err_free_queue;

#ifdef HAVE_PAN_KMOD_KBASE
   const bool uses_kbase = gpu_queue_uses_kbase(dev);
#else
   const bool uses_kbase = false;
#endif
   bool group_created = false;
   bool tiler_initialized = false;

   if (!uses_kbase) {
      int ret = pan_kmod_sync_create(dev->kmod.dev, 0, &queue->syncobj_handle);
      if (ret) {
         result = panvk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to create our internal sync object");
         goto err_finish_queue;
      }
   }

   const VkDeviceQueueShaderCoreControlCreateInfoARM *core_ctrl =
      vk_find_struct_const(create_info->pNext,
                           DEVICE_QUEUE_SHADER_CORE_CONTROL_CREATE_INFO_ARM);

#ifdef HAVE_PAN_KMOD_KBASE
   if (uses_kbase) {
      result = kbase_create_group(queue);
      if (result != VK_SUCCESS)
         goto err_destroy_syncobj;
      group_created = true;

      result = init_tiler(queue);
      if (result != VK_SUCCESS)
         goto err_cleanup_created;
      tiler_initialized = true;
   } else
#endif
   {
      result = init_tiler(queue);
      if (result != VK_SUCCESS)
         goto err_destroy_syncobj;
      tiler_initialized = true;

      result = create_group(queue, get_panthor_group_priority(create_info),
                            core_ctrl ? core_ctrl->shaderCoreCount : 0);
      if (result != VK_SUCCESS)
         goto err_cleanup_created;
      group_created = true;
   }

   result = init_queue(queue);
   if (result != VK_SUCCESS)
      goto err_cleanup_created;

   queue->vk.driver_submit = panvk_per_arch(gpu_queue_submit);
   *out_queue = &queue->vk;
   return VK_SUCCESS;

err_cleanup_created:
#ifdef HAVE_PAN_KMOD_KBASE
   if (uses_kbase) {
      /* Groups before heaps, so no firmware CS still references a heap
       * context when it is destroyed. */
      if (group_created)
         kbase_destroy_group(queue);
      if (tiler_initialized)
         cleanup_tiler(queue);
   } else
#endif
   {
      if (group_created)
         destroy_group(queue);
      if (tiler_initialized)
         cleanup_tiler(queue);
   }

err_destroy_syncobj:
   if (!uses_kbase)
      pan_kmod_sync_destroy(dev->kmod.dev, queue->syncobj_handle);

err_finish_queue:
   vk_queue_finish(&queue->vk);

err_free_queue:
   vk_free(&dev->vk.alloc, queue);
   return result;
}

void
panvk_per_arch(destroy_gpu_queue)(struct vk_queue *vk_queue)
{
   struct panvk_gpu_queue *queue = container_of(vk_queue, struct panvk_gpu_queue, vk);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev))
      kbase_queue_wait_current(queue, UINT64_MAX);
#endif

   cleanup_queue(queue);
#ifdef HAVE_PAN_KMOD_KBASE
   if (gpu_queue_uses_kbase(dev)) {
      /* Terminate the groups first so no firmware CS can still hold a
       * HEAP_SET reference when the tiler heap contexts are destroyed. */
      kbase_destroy_group(queue);
      cleanup_tiler(queue);
   } else
#endif
   {
      destroy_group(queue);
      cleanup_tiler(queue);
      pan_kmod_sync_destroy(dev->kmod.dev, queue->syncobj_handle);
   }
   vk_queue_finish(&queue->vk);
   vk_free(&dev->vk.alloc, queue);
}

VkResult
panvk_per_arch(gpu_queue_check_status)(struct vk_queue *vk_queue)
{
   struct panvk_gpu_queue *queue = container_of(vk_queue, struct panvk_gpu_queue, vk);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct drm_panthor_group_get_state state = {
      .group_handle = queue->group_handle,
   };

#ifdef HAVE_PAN_KMOD_KBASE
   /* kbase reports CSF faults through the notification stream consumed by
    * completion waits.  Check its error latch instead of issuing three
    * MEM_SYNC invalidations and reading the GPU-written subqueue contexts on
    * every Vulkan status query. */
   if (gpu_queue_uses_kbase(dev)) {
      if (kbase_kmod_csf_has_error(dev->kmod.dev)) {
         u_printf_with_ctx(stdout, &dev->printf.ctx);
         return vk_queue_set_lost(&queue->vk,
                                  "kbase: CSF queue-group error");
      }

      return VK_SUCCESS;
   }
#endif

   /* check for CS error and treat it as device lost */
   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      panvk_priv_mem_readback(queue->subqueues[i].context, 0,
                              struct panvk_cs_subqueue_context, subq_ctx) {
         if (subq_ctx->last_error != 0) {
            /* Check printf buffer one more time before exiting */
            u_printf_with_ctx(stdout, &dev->printf.ctx);

            return vk_queue_set_lost(&queue->vk, "CS_FAULT");
         }
      }
   }

   int ret = pan_kmod_ioctl(dev->drm_fd, DRM_IOCTL_PANTHOR_GROUP_GET_STATE,
                            &state);
   if (!ret && !state.state)
      return VK_SUCCESS;

   /* Check printf buffer one more time before exiting */
   u_printf_with_ctx(stdout, &dev->printf.ctx);

   vk_queue_set_lost(&queue->vk,
                     "group state: err=%d, state=0x%x, fatal_queues=0x%x", ret,
                     state.state, state.fatal_queues);

   return VK_ERROR_DEVICE_LOST;
}
