/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_device.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "../../lib/pan_trace_gate.h"
#include "genxml/gen_macros.h"

#include "decode.h"

#include "kmod/kbase_kmod.h"

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"
#include "panvk_queue.h"
#include "util/os_time.h"

#include "vk_framebuffer.h"
#include "vk_sync.h"

#include "kmod/pan_kmod.h"

#include "drm-uapi/panfrost_drm.h"
/* ---- P4 trace ---- */
#include <stdio.h>
#include <unistd.h>

/* ---- PANVK_TRACE gate: debug fprintf(stderr) only when PANVK_TRACE=1 ---- */
#include <stdio.h>
#include <stdlib.h>
#ifdef PANVK_TRACE_BUILD
static inline int panvk_trace_on_(void) { static int v = -1; if (v < 0) { const char *e = getenv("PANVK_TRACE"); v = (e && e[0] == '1'); } return v; }
#else
#define panvk_trace_on_() 0
#endif
#define fprintf(f, ...) (((f) == stderr && !panvk_trace_on_()) ? 0 : fprintf(f, __VA_ARGS__))
/* ---- end gate ---- */
#define P4_WAIT(d, a, t) ({ uint64_t _p4a = (a); long long _p4t = (t); \
   (void)0; \
   __auto_type _p4r = kbase_kmod_wait_atom((d), _p4a, _p4t); \
   (void)0; \
   _p4r; })
/* ---- end P4 ---- */

/* kbase job atoms don't support GPU-side pre_dep chaining the way DRM
 * syncobjs do (kbase_kmod_job_submit() always leaves pre_dep unset), so
 * ordering between the vertex/tiler job and the fragment job within one
 * batch is enforced with a CPU-side wait here. Ordering against *other*
 * queue submissions is instead handled lazily: we never block here, we
 * just record the atom numbers and hand them to panvk_kbase_sync_set_pending()
 * so a future vk_sync_wait() on queue->sync (or a signal semaphore) does
 * the actual blocking, off the submission's critical path. */

/* v67b: vertex/tiler work overlaps the previous pass (PANVK_OVERLAP=0: off) */
#include <stdlib.h>
static __attribute__((unused)) bool
panvk_overlap_enabled(void)
{
   static int on = -1;
   if (on < 0) {
      const char *e = getenv("PANVK_OVERLAP");
      on = !(e && e[0] == '0');
   }
   return on;
}

/* Dependency type on an earlier GPU atom of this queue. Within one
 * vkQueueSubmit it is DATA, so a GPU fault fails the rest of the submit and
 * reaches its fences and exported sync_files (only the last
 * PANVK_KBASE_SYNC_TARGET_COUNT batch atoms are waited on). Atoms of
 * earlier submits and semaphore waits stay ORDER: a fault there must not
 * fail unrelated later work. From bmdhacks/malisx2 (MIT). */
static uint8_t
dep_type(const struct panvk_gpu_queue *queue, uint64_t atom)
{
#if PAN_ARCH == 9
   if (atom && atom < 256 && BITSET_TEST(queue->submit_atoms, atom))
      return BASE_JD_DEP_TYPE_DATA;
#endif
   return BASE_JD_DEP_TYPE_ORDER;
}

static void
note_submitted(struct panvk_gpu_queue *queue, uint64_t atom)
{
#if PAN_ARCH == 9
   if (atom && atom < 256)
      BITSET_SET(queue->submit_atoms, atom);
#endif
}

/* A timestamp must follow the fragment work of the previous batches, so such
 * a batch never takes the PANVK_OVERLAP path. */
static inline bool
batch_has_timestamps(const struct panvk_batch *batch)
{
   return batch->timestamps || batch->after_frag;
}

/* Reset a batch that has already been issued. flush = clean the descriptor
 * pool after it; a caller that resets several batches can pass false and
 * flush once after the last one. */
static void
panvk_reset_issued_batch(struct panvk_cmd_buffer *cmdbuf,
                         struct panvk_batch *batch, bool flush)
{
   {
      util_dynarray_foreach(&batch->jobs, void *, job)
         memset((*job), 0, 4 * 4);

#if PAN_ARCH == 9
      /* A MALLOC_VERTEX job writes the pointer and strides of the vertex
       * packets it allocated into its own Draw vertex array (the CPU only
       * sets Packet). Left in place, the next run of the same command buffer
       * starts from that stale pointer and the job faults. Put back what
       * the CPU wrote. */
      util_dynarray_foreach(&batch->malloc_vertex_jobs, void *, job) {
         struct mali_vertex_array_packed *va =
            (void *)((uint8_t *)(*job) +
                     pan_section_offset(MALLOC_VERTEX_JOB, DRAW) + 2 * 4);
         pan_pack(va, VERTEX_ARRAY, cfg)
            cfg.packet = true;
      }
#endif

      /* Reset the tiler before re-issuing the batch */
      if (batch->tiler.ctx_descs.cpu) {
         memcpy(batch->tiler.heap_desc.cpu, &batch->tiler.heap_templ,
                sizeof(batch->tiler.heap_templ));

         struct mali_tiler_context_packed *ctxs = batch->tiler.ctx_descs.cpu;

         for (uint32_t i = 0; i < batch->fb.layer_count; i++)
            memcpy(&ctxs[i], &batch->tiler.ctx_templ, sizeof(*ctxs));
      }

      /* jm-v7 106: the indirect varying allocators start over. */
      util_dynarray_foreach(&batch->vary_infos, uint32_t *, offset)
         **offset = 0;

      /* We don't keep track of BO <-> job relationship, so let's just flush the
       * whole desc pool for now. */
      if (flush)
         panvk_pool_flush_maps(&cmdbuf->desc_pool);
   }

   batch->issued = false;
}

static uint64_t
panvk_queue_submit_batch(struct panvk_gpu_queue *queue,
                         struct panvk_cmd_buffer *cmdbuf,
                         struct panvk_batch *batch, struct pan_kmod_bo **bos,
                         unsigned nr_bos, uint64_t *out_frag_atom)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   uint64_t vtc_atom = 0, frag_atom = 0;
   (void)0;

   /* Reset the batch if it's already been issued */
   if (batch->issued)
      panvk_reset_issued_batch(cmdbuf, batch, true);

   /* Flush pending synchronization requests before submitting the job, to
    * make sure things are GPU-visible. */
   pan_kmod_flush_bo_map_syncs(dev->kmod.dev);

   if (batch->vtc_jc.first_job) {
      uint32_t vtc_core_req = (BASE_JD_REQ_CS | BASE_JD_REQ_T | BASE_JD_REQ_V);
      /* Without PERMON kbase leaves the GPU timestamp counter stopped. */
      if (batch->timestamps)
         vtc_core_req |= BASE_JD_REQ_PERMON;
         if (unlikely(!queue->warmed_up)) {
            /* The first vertex/tiler/compute chain must be submitted too
             * (it can be a buffer upload the app relies on). Submit it
             * synchronously, the same way the first fragment job is. */
            if (queue->in_dep) {
               kbase_kmod_wait_atom(dev->kmod.dev, queue->in_dep, -1);
               queue->in_dep = 0;
            }
            kbase_kmod_job_submit_retry(dev->kmod.dev, batch->vtc_jc.first_job,
                                        vtc_core_req, bos, nr_bos, NULL, 0, 5);
            vtc_atom = 0; /* already waited for */
            queue->warmed_up = true;
         } else {
            const bool v67_ov = panvk_overlap_enabled() && batch->frag_jc.first_job &&
                                !queue->in_dep && queue->frag_warmed_up && /* v67b */
                                !batch_has_timestamps(batch);
            const uint64_t dep0 =
                v67_ov ? queue->half_frag_atom[batch->heap_half & 1] : /* v67b */
                (queue->last_any_atom ? queue->last_any_atom : queue->last_frag_atom);
            vtc_atom = kbase_kmod_job_submit_dep(dev->kmod.dev, batch->vtc_jc.first_job, vtc_core_req, bos, nr_bos,
                dep0, dep_type(queue, dep0),
                v67_ov ? queue->last_vtc_atom : queue->in_dep,
                v67_ov ? dep_type(queue, queue->last_vtc_atom) : BASE_JD_DEP_TYPE_ORDER,
                batch->frag_jc.first_job != 0 && queue->frag_warmed_up);
            assert(vtc_atom);
            note_submitted(queue, vtc_atom);
            queue->last_vtc_atom = vtc_atom; /* v67b */
            queue->in_dep = 0;
            queue->last_any_atom = vtc_atom;

      if (PANVK_DEBUG(TRACE) || PANVK_DEBUG(SYNC)) {
         ASSERTED bool done =
            P4_WAIT(dev->kmod.dev, vtc_atom, -1);
         assert(done);
            vtc_atom = 0; /* already waited+consumed above; don't wait again in wait_one() */

         /* If we want to read the descriptors back, we need to invalidate the
          * whole desc pool, otherwise we might end up with stale data. */
         panvk_pool_invalidate_maps(&cmdbuf->desc_pool);
         pan_kmod_flush_bo_map_syncs(dev->kmod.dev);
      }

      if (PANVK_DEBUG(TRACE)) {
         /* skip pandecode_jc */
      }

      if (PANVK_DEBUG(DUMP))
         pandecode_dump_mappings(dev->debug.decode_ctx);

      if (PANVK_DEBUG(SYNC))
         pandecode_abort_on_fault(dev->debug.decode_ctx, batch->vtc_jc.first_job,
                                  phys_dev->kmod.dev->props.gpu_id);
   
         } /* end !warmed_up else */
      }

   if (batch->frag_jc.first_job) {
      /* The fragment job depends on the vertex/tiler job. kbase atoms can't
       * express that dependency for us, so block on the CPU here before
       * submitting the fragment job. */
      if (vtc_atom && unlikely(!queue->frag_warmed_up)) {
         bool done = P4_WAIT(dev->kmod.dev, vtc_atom, 16000000);
         (void)0;

         if (!done) {
            (void)0;
         }
      }

      if (unlikely(!queue->frag_warmed_up)) {
         if (queue->in_dep) {
            kbase_kmod_wait_atom(dev->kmod.dev, queue->in_dep, -1);
            queue->in_dep = 0;
         }
         (void)0;
         bool ok = kbase_kmod_job_submit_retry(
            dev->kmod.dev, batch->frag_jc.first_job, BASE_JD_REQ_FS,
            bos, nr_bos, NULL, 0, 5);
         queue->frag_warmed_up = true;
         (void)0;
         frag_atom = 0;
      } else {
         const bool frag_ov =
            panvk_overlap_enabled() && vtc_atom && !queue->in_dep; /* v67b */
         frag_atom = kbase_kmod_job_submit_dep(dev->kmod.dev, batch->frag_jc.first_job,
                                             BASE_JD_REQ_FS, bos, nr_bos,
                                             vtc_atom ? vtc_atom : queue->last_any_atom,
                                             vtc_atom ? BASE_JD_DEP_TYPE_DATA
                                                      : dep_type(queue, queue->last_any_atom),
                                             frag_ov ? queue->last_frag_atom : queue->in_dep,
                                             frag_ov ? dep_type(queue, queue->last_frag_atom)
                                                     : BASE_JD_DEP_TYPE_ORDER,
                                             false);
         assert(frag_atom);
         note_submitted(queue, frag_atom);
         queue->half_frag_atom[batch->heap_half & 1] = frag_atom; /* v67b */
         queue->last_frag_atom = frag_atom; queue->last_any_atom = frag_atom;
         if (!vtc_atom) {
            static int fo = 0;
            if (fo++ < 3) (void)0;
         }
         queue->in_dep = 0;
      }

      (void)0;
      if (PANVK_DEBUG(TRACE))
         /* skip pandecode_jc */

      if (PANVK_DEBUG(DUMP))
         pandecode_dump_mappings(dev->debug.decode_ctx);

      /* tex4: do not pandecode fragment before wait */
   }

   if (PANVK_DEBUG(TRACE))
      pandecode_next_frame(dev->debug.decode_ctx);

   batch->issued = true;

   *out_frag_atom = frag_atom;
   return frag_atom ? frag_atom : vtc_atom;
}

/* kbase has no kernel wait primitive for VkEvent (see panvk_event.h), so
 * events are tracked as a host-side flag plus the kbase atom of the work a
 * vkCmdSetEvent() follows. A wait spins only on events set from the host:
 * a pending vkCmdSetEvent() on this queue is ordered before the waiting
 * batch by the atom dependencies. */
static void
panvk_wait_event_ops(struct panvk_batch *batch)
{
   util_dynarray_foreach(&batch->event_ops, struct panvk_cmd_event_op, op) {
      if (op->type != PANVK_EVENT_OP_WAIT)
         continue;

      while (!op->event->signaled && !op->event->signal_atom)
         usleep(100);
   }
}

/* vkCmdSetEvent() takes effect when the GPU has completed the work recorded
 * before it (atom), not at submit time: vkGetEventStatus() polls the atom.
 * atom == 0 means that work already completed. */
static void
panvk_apply_event_ops(struct panvk_batch *batch, uint64_t atom)
{
   util_dynarray_foreach(&batch->event_ops, struct panvk_cmd_event_op, op) {
      switch (op->type) {
      case PANVK_EVENT_OP_SET:
         if (op->event->signaled)
            break;
         if (atom)
            op->event->signal_atom = atom;
         else
            op->event->signaled = true;
         break;
      case PANVK_EVENT_OP_RESET:
         op->event->signaled = false;
         op->event->signal_atom = 0;
         break;
      case PANVK_EVENT_OP_WAIT:
         /* Handled before submission in panvk_wait_event_ops(). */
         break;
      default:
         UNREACHABLE("bad panvk_cmd_event_op type\n");
      }
   }
}

/* wait_func passed to panvk_kbase_sync_set_pending(): blocks on whichever
 * kbase atom numbers are non-zero in targets[]. */
static VkResult
panvk_jm_kbase_wait_atoms(void *data, const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
                          uint64_t abs_timeout_ns)
{
   struct pan_kmod_dev *kmod_dev = data;

   /* vk_sync deadlines are CLOCK_MONOTONIC (os_time). MONOTONIC_RAW drifts
    * from it with uptime, which turned timeouts into DEVICE_LOST. This was
    * fixed on v9 first; jm-v7 093 uses CLOCK_MONOTONIC on v6/v7 as well.
    * PANVK_V7_RAW_CLOCK=1 restores MONOTONIC_RAW there (A/B only).
    */
#if PAN_ARCH == 9
   const clockid_t clk = CLOCK_MONOTONIC;
#else
   static int raw_clock = -1;
   if (raw_clock < 0) {
      const char *e = getenv("PANVK_V7_RAW_CLOCK");
      raw_clock = e && e[0] == '1';
   }
   const clockid_t clk = raw_clock ? CLOCK_MONOTONIC_RAW : CLOCK_MONOTONIC;
#endif

   for (unsigned i = 0; i < PANVK_KBASE_SYNC_TARGET_COUNT; i++) {
      if (!targets[i])
         continue;

      int64_t timeout_ns = -1;
      if (abs_timeout_ns != UINT64_MAX) {
         struct timespec now;
         clock_gettime(clk, &now);
         int64_t now_ns = (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec;
         timeout_ns = MAX2((int64_t)abs_timeout_ns - now_ns, 0);
      }

      if (!P4_WAIT(kmod_dev, targets[i], timeout_ns))
         {
         if (abs_timeout_ns != UINT64_MAX) {
            struct timespec now2;
            clock_gettime(clk, &now2);
            int64_t now2_ns = (int64_t)now2.tv_sec * 1000000000ll + now2.tv_nsec;
            if (now2_ns >= (int64_t)abs_timeout_ns)
               return VK_TIMEOUT;
         }
         return VK_ERROR_DEVICE_LOST;
      }
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(gpu_queue_submit)(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   struct panvk_gpu_queue *queue = container_of(vk_queue, struct panvk_gpu_queue, vk);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   (void)0;

   uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT] = {0};
   unsigned ntargets = 0;

#if PAN_ARCH == 9
   BITSET_ZERO(queue->submit_atoms);
#endif
   /* The list is empty unless imports were listed (v9 by default, v6/v7
    * with PANVK_KBASE_IMPORT_SYNC=1, jm-v7 094): then only a lock. */
   if (submit->command_buffer_count)
      kbase_kmod_jm_clean_imports(dev->kmod.dev);

   if (submit->wait_count) {
      (void)0;

      VkResult result = VK_SUCCESS;
      uint64_t in_atoms[8];
      unsigned n_in = 0;
      for (uint32_t w = 0; w < submit->wait_count && result == VK_SUCCESS; w++) {
         uint64_t wt[PANVK_KBASE_SYNC_TARGET_COUNT] = {0};
         int k = panvk_kbase_sync_gpu_wait_targets(submit->waits[w].sync, wt);
         if (k == 1)
            continue;
         if (k == 2) {
            for (unsigned t = 0; t < PANVK_KBASE_SYNC_TARGET_COUNT; t++) {
               if (!wt[t])
                  continue;
               if (n_in < ARRAY_SIZE(in_atoms))
                  in_atoms[n_in++] = wt[t];
               else
                  kbase_kmod_wait_atom(dev->kmod.dev, wt[t], -1);
            }
            continue;
         }
         /* wait-before-signal or foreign sync: fall back to a CPU wait */
         result = vk_sync_wait(&dev->vk, submit->waits[w].sync,
                               submit->waits[w].wait_value,
                               VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
      }
      /* One atom goes to the GPU as a pre-dependency, extra ones are
       * waited on the CPU (rare). */
      for (unsigned a = 1; a < n_in; a++)
         kbase_kmod_wait_atom(dev->kmod.dev, in_atoms[a], -1);
      queue->in_dep = n_in ? in_atoms[0] : 0;

      (void)0;

      if (result != VK_SUCCESS)
         return result;
   }

   for (uint32_t j = 0; j < submit->command_buffer_count; ++j) {
      struct panvk_cmd_buffer *cmdbuf =
         container_of(submit->command_buffers[j], struct panvk_cmd_buffer, vk);

#if PAN_ARCH <= 9
      /* A conditional rendering predicate job rewrites job headers of the
       * later batches of its draw (cmd_cond_render_end()). Reset (CPU write
       * plus cache flush) every batch before any of them runs, so that no
       * flush lands after a predicate job wrote a header.
       *
       * Every reset write is in the descriptor pool, which the old code
       * cleaned whole once per batch (256 times per PanProbe tess_perf frame
       * on v6/v7). DC CVAC only writes back dirty lines, so one clean after
       * the last reset, still before the first submit, leaves memory in the
       * same state. PANVK_JM_RESET_FLUSH=batch (or 0) = once per batch.
       */
      const bool flush_once = dev->jm_opt.reset_flush_once;
      bool reset_any = false;
      list_for_each_entry(struct panvk_batch, batch, &cmdbuf->batches, node) {
         if (batch->issued) {
            panvk_reset_issued_batch(cmdbuf, batch, !flush_once);
            reset_any = true;
         }
      }
      if (reset_any && flush_once)
         panvk_pool_flush_maps(&cmdbuf->desc_pool);
#endif

      list_for_each_entry(struct panvk_batch, batch, &cmdbuf->batches, node) {
         /* jm-v7 113: kbase JM atoms carry no BO list (residency is the
          * kbase context's), and kbase_kmod only copied it into an unused
          * slot field, so the per-batch gathering of every pool BO and its
          * O(n^2) dedup were dead work.
          */
         struct pan_kmod_bo **bos = NULL;
         unsigned nr_bos = 0;

         panvk_wait_event_ops(batch);

         uint64_t frag_atom = 0;
         uint64_t last_atom =
            panvk_queue_submit_batch(queue, cmdbuf, batch, bos, nr_bos,
                                     &frag_atom);

         if (last_atom) {
            queue->last_submitted_atom = last_atom;
            if (ntargets < PANVK_KBASE_SYNC_TARGET_COUNT) {
               targets[ntargets++] = last_atom;
            } else {
               /* targets[] is full (more than PANVK_KBASE_SYNC_TARGET_COUNT
                * batches in this submit) -- instead of silently dropping
                * this atom (the old bug: a dropped frag_atom means nobody
                * ever waits for that fragment job), wait for it
                * synchronously right here, same trick already used for
                * vtc_atom above. */
               /* v50c: atoms are fully ordered on this queue, the newest one covers
                * all earlier ones -- no CPU wait on the submit path. */
               kbase_kmod_atom_release(dev->kmod.dev, targets[PANVK_KBASE_SYNC_TARGET_COUNT - 1]);
               targets[PANVK_KBASE_SYNC_TARGET_COUNT - 1] = last_atom;
            }
         }

         panvk_apply_event_ops(batch, last_atom ? last_atom
                                                : queue->last_submitted_atom);
      }
   }


   if (queue->in_dep) {
      if (ntargets < PANVK_KBASE_SYNC_TARGET_COUNT)
         targets[ntargets++] = queue->in_dep;
      else
         kbase_kmod_wait_atom(dev->kmod.dev, queue->in_dep, -1);
      queue->in_dep = 0;
   }
   if (ntargets == 0 && queue->last_submitted_atom)
      targets[ntargets++] = queue->last_submitted_atom;

   panvk_kbase_sync_set_pending(queue->sync, dev->kmod.dev,
                                panvk_jm_kbase_wait_atoms, targets, NULL);

   for (unsigned i = 0; i < submit->signal_count; i++) {
      panvk_kbase_sync_set_pending(submit->signals[i].sync, dev->kmod.dev,
                                   panvk_jm_kbase_wait_atoms, targets, NULL);
   }

   /* PANVK_GPU_STATS: JM chains have no cheap GPU timestamps (they need
    * PERMON and write jobs), so the busy time is the CPU-observed time with
    * atoms in flight. It is reported as consecutive intervals on a busy-time
    * axis shared by the device's queues, so their union is that time. */
   if (panvk_gpu_stats_path()) {
      const struct panvk_physical_device *pdev =
         to_panvk_physical_device(dev->vk.physical);
      const uint64_t busy = kbase_kmod_jm_busy_ns(dev->kmod.dev);
      panvk_gpu_stats_busy(1000000000ull,
                           p_atomic_read_relaxed(&pdev->memory.heap_used),
                           queue->stats_busy_ns, busy);
      queue->stats_busy_ns = busy;
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(create_gpu_queue)(struct panvk_device *device,
                                 const VkDeviceQueueCreateInfo *create_info,
                                 uint32_t queue_idx,
                                 struct vk_queue **out_queue)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(device->vk.physical);
   ASSERTED const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(create_info->pNext,
                          DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   ASSERTED const VkQueueGlobalPriorityKHR priority =
      priority_info ? priority_info->globalPriority
                    : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;

   /* XXX: Panfrost kernel module doesn't support priorities so far */
   assert(priority == VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR);

   struct panvk_gpu_queue *queue = vk_zalloc(&device->vk.alloc, sizeof(*queue), 8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!queue)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_queue_init(&queue->vk, &device->vk, create_info, queue_idx);
   if (result != VK_SUCCESS)
      goto err_free_queue;

   /* Start signaled: with nothing submitted yet, the queue is trivially idle. */
   result = vk_sync_create(&device->vk, &phys_dev->drm_syncobj_type, 0, 1,
                           &queue->sync);
   if (result != VK_SUCCESS)
      goto err_finish_queue;

   queue->vk.driver_submit = panvk_per_arch(gpu_queue_submit);
   if (panvk_gpu_stats_path())
      kbase_kmod_jm_busy_track(device->kmod.dev);
   *out_queue = &queue->vk;
   return VK_SUCCESS;

err_finish_queue:
   vk_queue_finish(&queue->vk);

err_free_queue:
   vk_free(&device->vk.alloc, queue);
   return result;
}

void panvk_per_arch(destroy_gpu_queue)(struct vk_queue *vk_queue)
{
   struct panvk_gpu_queue *queue = container_of(vk_queue, struct panvk_gpu_queue, vk);
   struct panvk_device *dev = to_panvk_device(vk_queue->base.device);

#if PAN_ARCH == 9
   /* Device teardown frees the tiler heap and pools next: give jobs still in
    * flight (app skipped vkDeviceWaitIdle, or exits mid-frame) up to 2 s
    * instead of unmapping memory under them (bmdhacks/malisx2, MIT). */
   if (vk_sync_wait(&dev->vk, queue->sync, 0, VK_SYNC_WAIT_COMPLETE,
                    os_time_get_absolute_timeout(2000000000ll)) == VK_TIMEOUT)
      mesa_logw("panvk: JM queue destroyed with jobs still running");
#endif

   vk_queue_finish(&queue->vk);
   vk_sync_destroy(&dev->vk, queue->sync);
   vk_free(&dev->vk.alloc, queue);
}

VkResult
panvk_per_arch(gpu_queue_check_status)(struct vk_queue *vk_queue)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(QueueWaitIdle)(VkQueue _queue)
{
   VK_FROM_HANDLE(panvk_gpu_queue, queue, _queue);
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);

   /* we need to use vk_common_QueueWaitIdle if we ever go threaded */
   assert(queue->vk.submit.mode != VK_QUEUE_SUBMIT_MODE_THREADED);

   if (vk_device_is_lost(&dev->vk)) {
      /* Check printf buffer one more time before exiting */
      u_printf_with_ctx(stdout, &dev->printf.ctx);
      return VK_ERROR_DEVICE_LOST;
   }

   VkResult result = vk_sync_wait(&dev->vk, queue->sync, 0,
                                  VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
   if (result != VK_SUCCESS)
      return VK_ERROR_DEVICE_LOST;

   return VK_SUCCESS;
}
