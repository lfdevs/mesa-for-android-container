/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_QUEUE_H
#define PANVK_QUEUE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "genxml/gen_macros.h"

#include <stdint.h>

#include "panvk_device.h"

#include "vk_queue.h"

enum panvk_subqueue_id {
   PANVK_SUBQUEUE_VERTEX_TILER = 0,
   PANVK_SUBQUEUE_FRAGMENT,
   PANVK_SUBQUEUE_COMPUTE,
   PANVK_SUBQUEUE_COUNT,
};

struct panvk_tiler_heap {
   uint32_t chunk_size;
   struct panvk_priv_mem desc;
   struct panvk_priv_mem oom_fbd;
   struct {
      uint32_t handle;
      uint64_t dev_addr;
   } context;
};

struct panvk_subqueue {
   struct panvk_priv_mem context;
   uint32_t *reg_file;

   /* Memory to save/restore CS registers in functions/exception handlers.
    * Because registers are dumped to a fixed address rather than a moving
    * stack pointer, nested function/exception handler calls are not supported.
    */
   struct panvk_priv_mem regs_save;


   struct {
      /* Mask of resources requested by this subqueue. */
      uint32_t mask;
      /* Address and size of the linear buffer containing REQ_RESOURCE. */
      uint32_t cs_buffer_size;
      uint64_t cs_buffer_addr;
      /* Allocation */
      struct panvk_priv_mem buf;
   } req_resource;

   struct {
      struct pan_kmod_bo *bo;
      uint64_t size;
      struct {
         uint64_t dev;
         void *host;
      } addr;
   } tracebuf;

#ifdef HAVE_PAN_KMOD_KBASE
   /* kbase-backed queues manage the CS ring buffer in userspace (the
    * panthor kernel driver does the equivalent in kernel-owned rings). */
   struct {
      /* kbase compatibility path: each PanVK subqueue owns a CSG and uses
       * CSI0.  Some Android kbase stacks accept later CSIs into a scheduled
       * CSG but never execute them. */
      uint32_t group_handle;

      struct pan_kmod_bo *ringbuf_bo;
      void *ringbuf_cpu;
      uint64_t ringbuf_dev;

      /* USER_IO pages from KBASE_IOCTL_CS_QUEUE_BIND: doorbell page,
       * input page (CS_INSERT), output page (CS_EXTRACT/CS_ACTIVE). */
      void *user_io;

      /* Monotonically-increasing byte offset of the next ring entry. */
      uint64_t insert;

      /* Number of jobs emitted; each bumps the subqueue seqno cell by one
       * when it retires. */
      uint64_t emitted_jobs;

      uint32_t last_last_job_offset;
      uint32_t last_job_offset;
      uint32_t last_job_size;
      uint32_t last_job_entry_size;
      uint32_t last_stream_size;
      uint32_t last_flush_id;
      uint64_t last_stream_addr;

      struct panvk_priv_mem init_cs;
      uint64_t init_stream_addr;
      uint32_t init_stream_size;
      uint32_t init_flush_id;
      uint32_t init_pending;
   } kbase;
#endif
};

struct panvk_desc_ringbuf {
   struct panvk_priv_mem syncobj;
   struct pan_kmod_bo *bo;
   uint64_t size;
   struct {
      uint64_t dev;
      void *host;
   } addr;
};

struct panvk_gpu_queue {
   struct vk_queue vk;

   uint32_t group_handle;
   uint32_t syncobj_handle;

   struct panvk_tiler_heap tiler_heap;
   struct panvk_desc_ringbuf render_desc_ringbuf;
   struct panvk_priv_mem syncobjs;

#ifdef HAVE_PAN_KMOD_KBASE
   /* Per-subqueue completion seqno cells (panvk_cs_sync64 layout), bumped
    * by a SYNC_ADD64 at the end of every ring entry and polled by the CPU.
    * Must live in GPU-uncached memory: on inner-shareable pages the sync
    * write can linger in the GPU L2 where the CPU never sees it. */
   struct {
      struct pan_kmod_bo *bo;
      void *cpu;
      uint64_t dev;
   } kbase_seqnos;
   uint32_t kbase_tiler_submit_count;
   uint64_t kbase_tiler_work_count;
   /* Tiler heap context retired by the last heap renewal.  The firmware CS
    * HEAP_SET registers keep pointing at the old context until each graphics
    * subqueue executes its next ring entry, so the context is only
    * destroyed once both graphics subqueues have provably re-armed HEAP_SET
    * (their emitted-job counts passed the values recorded at retirement and
    * the pre-renewal drain completed).  ctx == 0 means the slot is empty. */
   struct {
      uint64_t ctx;
      uint64_t vt_jobs;
      uint64_t frag_jobs;
   } kbase_retired_heap;
#endif

   struct {
      struct vk_sync *sync;
      uint64_t next_value;
   } utrace;

   struct panvk_subqueue subqueues[PANVK_SUBQUEUE_COUNT];
};

VK_DEFINE_HANDLE_CASTS(panvk_gpu_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

VkResult panvk_per_arch(create_gpu_queue)(
   struct panvk_device *dev, const VkDeviceQueueCreateInfo *create_info,
   uint32_t queue_idx, struct vk_queue **out_queue);
void panvk_per_arch(destroy_gpu_queue)(struct vk_queue *vk_queue);
VkResult panvk_per_arch(gpu_queue_submit)(struct vk_queue *vk_queue,
                                          struct vk_queue_submit *vk_submit);
VkResult panvk_per_arch(gpu_queue_check_status)(struct vk_queue *vk_queue);

#endif
