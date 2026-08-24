/*
 * Copyright © 2019 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "drm.h"
#include "v3dv_device.h"
#include "v3dv_cmd_buffer.h"
#include "v3dv_image.h"
#include "v3dv_entrypoints.h"
#include "v3dv_version_dispatch.h"
#include <xf86drm.h>

#include "broadcom/clif/clif_dump.h"
#include "broadcom/common/v3d_submit_util.h"
#include "util/libsync.h"
#include "util/perf/cpu_trace.h"
#include "vulkan/vulkan_core.h"
#include "vk_drm_syncobj.h"

#include <time.h>

static void
v3dv_clif_dump(struct v3dv_device *device,
               struct v3dv_job *job,
               struct drm_v3d_submit_cl *submit)
{
   if (!(V3D_DBG(CL) ||
         V3D_DBG(CL_NO_BIN) ||
         V3D_DBG(CLIF)))
      return;

   struct log_stream *stream = mesa_log_streami();
   struct clif_dump *clif = clif_dump_init(&device->devinfo,
                                           stream,
                                           V3D_DBG(CL) ||
                                           V3D_DBG(CL_NO_BIN),
                                           V3D_DBG(CL_NO_BIN));

   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (void *)entry->key;
      char *name = ralloc_asprintf(NULL, "%s_0x%x",
                                   bo->name, bo->offset);

      bool ok = v3dv_bo_map(device, bo, bo->size);
      if (!ok) {
         mesa_loge("failed to map BO for clif_dump.\n");
         ralloc_free(name);
         goto free_clif;
      }
      clif_dump_add_bo(clif, name, bo->offset, bo->size, bo->map);

      ralloc_free(name);
   }

   clif_dump(clif, submit);

 free_clif:
   clif_dump_destroy(clif);
   mesa_log_stream_destroy(stream);
}

static uint32_t
gather_in_syncs(struct v3dv_queue *queue,
                struct v3dv_job *job,
                enum v3dv_queue_type queue_sync,
                struct vk_sync_wait *waits,
                unsigned wait_count,
                struct v3dv_submit_sync_info *sync_info,
                uint32_t *handles)
{
   uint32_t n_syncs = 0;
   uint32_t idx = 0;

   /* If this is the first job submitted to a given GPU queue in this cmd buf
    * batch, it has to wait on wait semaphores (if any) before running.
    */
   if (queue->last_job_syncs.first[queue_sync])
      n_syncs = sync_info->wait_count;

   /* If the serialize flag is set the job needs to be serialized in the
    * corresponding queues. Notice that we may implement transfer operations
    * as both CL or TFU jobs.
    *
    * FIXME: maybe we could track more precisely if the source of a transfer
    * barrier is a CL and/or a TFU job.
    */
   bool sync_csd  = job->serialize & V3DV_BARRIER_COMPUTE_BIT;
   bool sync_tfu  = job->serialize & V3DV_BARRIER_TRANSFER_BIT;
   bool sync_cl   = job->serialize & (V3DV_BARRIER_GRAPHICS_BIT |
                                      V3DV_BARRIER_TRANSFER_BIT);
   bool sync_cpu  = job->serialize & V3DV_BARRIER_CPU_BIT;

   /* first job waits */
   for (uint32_t i = 0; i < n_syncs; i++)
      handles[idx++] = vk_sync_as_drm_syncobj(sync_info->waits[i].sync)->syncobj;

   /* explicit call-site waits */
   for (unsigned i = 0; i < wait_count; i++)
      handles[idx++] = vk_sync_as_drm_syncobj(waits[i].sync)->syncobj;


   /* internal serialization barriers */
   if (sync_cl)
      handles[idx++] = queue->last_job_syncs.syncs[V3DV_QUEUE_CL];
   if (sync_csd)
      handles[idx++] = queue->last_job_syncs.syncs[V3DV_QUEUE_CSD];
   if (sync_tfu)
      handles[idx++] = queue->last_job_syncs.syncs[V3DV_QUEUE_TFU];
   if (sync_cpu)
      handles[idx++] = queue->last_job_syncs.syncs[V3DV_QUEUE_CPU];

   return idx;
}

/* helpers for multisync common code */
static void *
multisync_zalloc(void *mem_ctx, size_t size)
{
   struct v3dv_device *device = mem_ctx;
   return vk_zalloc(&device->vk.alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
}

static void
multisync_free(void *mem_ctx, void *ptr)
{
   struct v3dv_device *device = mem_ctx;
   vk_free(&device->vk.alloc, ptr);
}

/* This function sets the extension for multiple in/out syncobjs. When it is
 * successful, it sets the extension id to DRM_V3D_EXT_ID_MULTI_SYNC.
 * Otherwise, the extension id is 0, which means an out-of-memory error.
 */
static bool
set_multisync(struct v3d_multisync *ms,
              struct v3dv_submit_sync_info *sync_info,
              struct vk_sync_wait *waits,
              unsigned wait_count,
              struct drm_v3d_extension *next,
              struct v3dv_queue *queue,
              struct v3dv_job *job,
              enum v3dv_queue_type in_queue_sync,
              enum v3dv_queue_type out_queue_sync,
              enum v3d_queue wait_stage)
{
   struct v3dv_device *device = queue->device;
   bool ret = false;

   /* Max input handles includes:
    * - All API waits that apply on the first job we submit to each queue.
    * - Any additional waits (i.e. pipeline barriers or dependencies between jobs)
    * - All the last_syncs we track per queue (if we need to drain all queues for a barrier)
    */
   uint32_t max_in_syncs = (sync_info ? sync_info->wait_count : 0) + wait_count +
                            V3DV_QUEUE_COUNT;

   assert(ms);
   ms->ops.zalloc = multisync_zalloc;
   ms->ops.free = multisync_free;
   ms->ops.mem_ctx = device;

   STACK_ARRAY(uint32_t, in_sync_handles, max_in_syncs);
   if (!in_sync_handles)
      return false;

   uint32_t total_in_syncs = gather_in_syncs(queue, job, in_queue_sync,
                                       waits, wait_count, sync_info, in_sync_handles);
   assert(total_in_syncs <= max_in_syncs);

   /* We always signal the syncobj from `device->last_job_syncs` related to
    * this v3dv_queue_type to track the last job submitted to this queue.
    */
   uint32_t out_sync_handle = queue->last_job_syncs.syncs[out_queue_sync];

   ret = v3d_multisync_init(ms, wait_stage,
                            in_sync_handles, total_in_syncs,
                            &out_sync_handle, 1, next);
   if (!ret)
      mesa_loge("Multisync Set Failed");

   STACK_ARRAY_FINISH(in_sync_handles);

   return ret;
}

static VkResult
handle_reset_query_cpu_job(struct v3dv_queue *queue,
                           struct v3dv_job *job,
                           struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   struct v3dv_device *device = queue->device;
   struct v3dv_reset_query_cpu_job_info *info = &job->cpu.query_reset;
   assert(info->pool);

   assert(info->pool->query_type != VK_QUERY_TYPE_OCCLUSION);
   assert(info->first + info->count <= info->pool->query_count);

   struct drm_v3d_submit_cpu submit = {0};
   struct v3d_multisync ms = {0};

   uint32_t *syncs = (uint32_t *) malloc(sizeof(uint32_t) * info->count);
   uintptr_t *kperfmon_ids = NULL;

   if (info->pool->query_type == VK_QUERY_TYPE_TIMESTAMP) {
      submit.bo_handle_count = 1;
      submit.bo_handles = (uintptr_t)(void *)&info->pool->timestamp.bo->handle;

      struct drm_v3d_reset_timestamp_query reset = {0};

      v3d_submit_ext_set(&reset.base, NULL, DRM_V3D_EXT_ID_CPU_RESET_TIMESTAMP_QUERY, 0);

      reset.count = info->count;
      reset.offset = info->pool->queries[info->first].timestamp.offset;

      for (uint32_t i = 0; i < info->count; i++) {
         struct v3dv_query *query = &info->pool->queries[info->first + i];
         syncs[i] = vk_sync_as_drm_syncobj(query->timestamp.sync)->syncobj;
      }

      reset.syncs = (uintptr_t)(void *)syncs;

      if (!set_multisync(&ms, sync_info, NULL, 0, (void *)&reset, queue, job,
                        V3DV_QUEUE_CPU, V3DV_QUEUE_CPU, V3D_CPU)) {
         free(syncs);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else {
      assert(info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR);
      struct drm_v3d_reset_performance_query reset = {0};

      v3d_submit_ext_set(&reset.base, NULL, DRM_V3D_EXT_ID_CPU_RESET_PERFORMANCE_QUERY, 0);

      struct vk_sync_wait waits[info->count];
      unsigned wait_count = 0;
      for (int i = 0; i < info->count; i++) {
         struct v3dv_query *query = &info->pool->queries[info->first + i];
         /* Only wait for a query if we've used it otherwise we will be
          * waiting forever for the fence to become signaled.
          */
         if (query->maybe_available) {
            waits[wait_count] = (struct vk_sync_wait){
               .sync = query->perf.last_job_sync
            };
            wait_count++;
         };
      }

      reset.count = info->count;
      reset.nperfmons = info->pool->perfmon.nperfmons;

      kperfmon_ids = (uintptr_t *) malloc(sizeof(uintptr_t) * info->count);

      for (uint32_t i = 0; i < info->count; i++) {
         struct v3dv_query *query = &info->pool->queries[info->first + i];

         syncs[i] = vk_sync_as_drm_syncobj(query->perf.last_job_sync)->syncobj;
         kperfmon_ids[i] = (uintptr_t)(void *)query->perf.kperfmon_ids;
      }

      reset.syncs = (uintptr_t)(void *)syncs;
      reset.kperfmon_ids = (uintptr_t)(void *)kperfmon_ids;

      if (!set_multisync(&ms, sync_info, waits, wait_count, (void *)&reset, queue, job,
                        V3DV_QUEUE_CPU, V3DV_QUEUE_CPU, V3D_CPU)) {
         free(syncs);
         free(kperfmon_ids);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   }

   submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
   submit.extensions = (uintptr_t)(void *)&ms;

   /* From the Vulkan spec for vkCmdResetQueryPool:
    *
    *    "This command defines an execution dependency between other query commands
    *     that reference the same query.
    *     ...
    *     The second synchronization scope includes all commands which reference the
    *     queries in queryPool indicated by firstQuery and queryCount that occur later
    *     in submission order."
    *
    * This means we should ensure that any timestamps after a reset don't execute before
    * the reset, however, for timestamps queries in particular we don't have to do
    * anything special because timestamp queries have to wait for all previously
    * submitted work to complete before executing (which we accomplish by using
    * V3DV_BARRIER_ALL on them) and that includes reset jobs submitted to the CPU queue.
    */
   int ret = v3d_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_CPU, &submit);

   free(syncs);
   free(kperfmon_ids);
   v3d_multisync_free(&ms);

   queue->last_job_syncs.first[V3DV_QUEUE_CPU] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CPU failed: %m");

   return VK_SUCCESS;
}

static VkResult
export_perfmon_last_job_sync(struct v3dv_queue *queue, struct v3dv_job *job, int *fd)
{
   int err;
   static const enum v3dv_queue_type queues_to_sync[] = {
      V3DV_QUEUE_CL,
      V3DV_QUEUE_CSD,
   };

   for (uint32_t i = 0; i < ARRAY_SIZE(queues_to_sync); i++) {
      enum v3dv_queue_type queue_type = queues_to_sync[i];
      int tmp_fd = -1;

      err = drmSyncobjExportSyncFile(job->device->pdevice->render_fd,
                                     queue->last_job_syncs.syncs[queue_type],
                                     &tmp_fd);

      if (err) {
         close(*fd);
         return vk_errorf(queue, VK_ERROR_UNKNOWN,
                          "sync file export failed: %m");
      }

      err = sync_accumulate("v3dv", fd, tmp_fd);

      if (err) {
         close(tmp_fd);
         close(*fd);
         return vk_errorf(queue, VK_ERROR_UNKNOWN,
                          "failed to accumulate sync files: %m");
      }
   }

   return VK_SUCCESS;
}

static VkResult
handle_end_query_cpu_job(struct v3dv_queue *queue, struct v3dv_job *job, uint32_t counter_pass_idx)
{
   MESA_TRACE_FUNC();
   VkResult result = VK_SUCCESS;

   mtx_lock(&job->device->query_mutex);

   struct v3dv_end_query_info *info = &job->cpu.query_end;

   int err = 0;
   int fd = -1;

   assert(info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR);

   if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      result = export_perfmon_last_job_sync(queue, job, &fd);

      if (result != VK_SUCCESS)
         goto fail;

      assert(fd >= 0);
   }

   for (uint32_t i = 0; i < info->count; i++) {
      assert(info->query + i < info->pool->query_count);
      struct v3dv_query *query = &info->pool->queries[info->query + i];

      if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
         uint32_t syncobj = vk_sync_as_drm_syncobj(query->perf.last_job_sync)->syncobj;
         err = drmSyncobjImportSyncFile(job->device->pdevice->render_fd,
                                        syncobj, fd);

         if (err) {
            result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                               "sync file import failed: %m");
            goto fail;
         }
      }

      query->maybe_available = true;
   }

fail:
   if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR)
      close(fd);

   cnd_broadcast(&job->device->query_ended);
   mtx_unlock(&job->device->query_mutex);

   return result;
}

static VkResult
handle_copy_query_results_cpu_job(struct v3dv_queue *queue,
                                  struct v3dv_job *job,
                                  struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   struct v3dv_device *device = queue->device;
   struct v3dv_copy_query_results_cpu_job_info *info =
      &job->cpu.query_copy_results;

   assert(info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR ||
          info->pool->query_type == VK_QUERY_TYPE_TIMESTAMP);

   assert(info->dst && info->dst->mem && info->dst->mem->bo);
   struct v3dv_bo *bo = info->dst->mem->bo;

   struct drm_v3d_submit_cpu submit = {0};
   struct v3d_multisync ms = {0};

   uint32_t *offsets = (uint32_t *) malloc(sizeof(uint32_t) * info->count);
   uint32_t *syncs = (uint32_t *) malloc(sizeof(uint32_t) * info->count);
   uint32_t *bo_handles = NULL;
   uintptr_t *kperfmon_ids = NULL;

   if (info->pool->query_type == VK_QUERY_TYPE_TIMESTAMP) {
      /* timestamp pool BO is V3DV-internal, never aliased by user BO. If
       * that could happen we would need to dedupe them
       */
      assert(bo->handle != info->pool->timestamp.bo->handle);
      submit.bo_handle_count = 2;

      bo_handles = (uint32_t *)
         malloc(sizeof(uint32_t) * submit.bo_handle_count);

      bo_handles[0] = bo->handle;
      bo_handles[1] = info->pool->timestamp.bo->handle;
      submit.bo_handles = (uintptr_t)(void *)bo_handles;

      struct drm_v3d_copy_timestamp_query copy = {0};

      v3d_submit_ext_set(&copy.base, NULL, DRM_V3D_EXT_ID_CPU_COPY_TIMESTAMP_QUERY, 0);

      copy.do_64bit = info->flags & VK_QUERY_RESULT_64_BIT;
      copy.do_partial = info->flags & VK_QUERY_RESULT_PARTIAL_BIT;
      copy.availability_bit = info->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
      copy.offset = info->offset + info->dst->mem_offset;
      copy.stride = info->stride;
      copy.count = info->count;

      for (uint32_t i = 0; i < info->count; i++) {
         assert(info->first < info->pool->query_count);
         assert(info->first + info->count <= info->pool->query_count);
         struct v3dv_query *query = &info->pool->queries[info->first + i];

         offsets[i] = query->timestamp.offset;
         syncs[i] = vk_sync_as_drm_syncobj(query->timestamp.sync)->syncobj;
      }

      copy.offsets = (uintptr_t)(void *)offsets;
      copy.syncs = (uintptr_t)(void *)syncs;

      if (!set_multisync(&ms, sync_info, NULL, 0, (void *)&copy, queue, job,
                        V3DV_QUEUE_CPU, V3DV_QUEUE_CPU, V3D_CPU)) {
         free(bo_handles);
         free(offsets);
         free(syncs);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   } else {
      assert(info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR);

      submit.bo_handle_count = 1;
      submit.bo_handles = (uintptr_t)(void *)&bo->handle;

      struct drm_v3d_copy_performance_query copy = {0};

      v3d_submit_ext_set(&copy.base, NULL, DRM_V3D_EXT_ID_CPU_COPY_PERFORMANCE_QUERY, 0);

      /* If the queryPool was created with VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR,
       * results for each query are written as an array of the type indicated
       * by VkPerformanceCounterKHR::storage for the counter being queried.
       * For v3dv, VkPerformanceCounterKHR::storage is
       * VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR.
       */
      copy.do_64bit = true;
      copy.do_partial = info->flags & VK_QUERY_RESULT_PARTIAL_BIT;
      copy.availability_bit = info->flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
      copy.offset = info->offset + info->dst->mem_offset;
      copy.stride = info->stride;
      copy.count = info->count;
      copy.nperfmons = info->pool->perfmon.nperfmons;
      copy.ncounters = info->pool->perfmon.ncounters;

      kperfmon_ids = (uintptr_t *) malloc(sizeof(uintptr_t) * info->count);

      struct vk_sync_wait waits[info->count];
      unsigned wait_count = 0;

      for (uint32_t i = 0; i < info->count; i++) {
         assert(info->first < info->pool->query_count);
         assert(info->first + info->count <= info->pool->query_count);
         struct v3dv_query *query = &info->pool->queries[info->first + i];

         syncs[i] = vk_sync_as_drm_syncobj(query->perf.last_job_sync)->syncobj;
         kperfmon_ids[i] = (uintptr_t)(void *)query->perf.kperfmon_ids;

         if (info->flags & VK_QUERY_RESULT_WAIT_BIT) {
               waits[wait_count] = (struct vk_sync_wait){
                  .sync = query->perf.last_job_sync
               };
               wait_count++;
         }
      }

      copy.syncs = (uintptr_t)(void *)syncs;
      copy.kperfmon_ids = (uintptr_t)(void *)kperfmon_ids;

      if (!set_multisync(&ms, sync_info, waits, wait_count, (void *)&copy, queue, job,
                        V3DV_QUEUE_CPU, V3DV_QUEUE_CPU, V3D_CPU)) {
         free(kperfmon_ids);
         free(bo_handles);
         free(offsets);
         free(syncs);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   }

   submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
   submit.extensions = (uintptr_t)(void *)&ms;

   int ret = v3d_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_CPU, &submit);

   free(kperfmon_ids);
   free(bo_handles);
   free(offsets);
   free(syncs);
   v3d_multisync_free(&ms);

   queue->last_job_syncs.first[V3DV_QUEUE_CPU] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CPU failed: %m");

   return VK_SUCCESS;
}

static VkResult
handle_timestamp_query_cpu_job(struct v3dv_queue *queue,
                               struct v3dv_job *job,
                               struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   struct v3dv_device *device = queue->device;

   assert(job->type == V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY);
   struct v3dv_timestamp_query_cpu_job_info *info = &job->cpu.query_timestamp;

   uint32_t *offsets =
      (uint32_t *) malloc(sizeof(uint32_t) * info->count);
   uint32_t *syncs =
      (uint32_t *) malloc(sizeof(uint32_t) * info->count);

   for (uint32_t i = 0; i < info->count; i++) {
      assert(info->query + i < info->pool->query_count);
      struct v3dv_query *query = &info->pool->queries[info->query + i];
      query->maybe_available = true;

      offsets[i] = query->timestamp.offset;
      syncs[i] = vk_sync_as_drm_syncobj(query->timestamp.sync)->syncobj;
   }

   struct v3d_multisync ms = {0};

   /* The CPU job should be serialized so it only executes after all previously
    * submitted work has completed
    */
   job->serialize = V3DV_BARRIER_ALL;

   if (!set_multisync(&ms, sync_info, NULL, 0, NULL, queue, job,
                     V3DV_QUEUE_CPU, V3DV_QUEUE_CPU, V3D_CPU)) {
      free(offsets);
      free(syncs);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   int ret = v3d_submit_timestamp_query_ioctl(
      device->pdevice->render_fd, info->pool->timestamp.bo->handle,
      offsets, syncs, info->count, &ms.ext.base);

   free(offsets);
   free(syncs);
   v3d_multisync_free(&ms);

   queue->last_job_syncs.first[V3DV_QUEUE_CPU] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CPU failed: %m");

   return VK_SUCCESS;
}

static VkResult
handle_csd_indirect_cpu_job(struct v3dv_queue *queue,
                            struct v3dv_job *job,
                            struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   struct v3dv_device *device = queue->device;

   assert(job->type == V3DV_JOB_TYPE_CPU_CSD_INDIRECT);
   struct v3dv_csd_indirect_cpu_job_info *info = &job->cpu.csd_indirect;
   assert(info->csd_job);

   assert(info->buffer && info->buffer->mem && info->buffer->mem->bo);
   struct v3dv_bo *bo = info->buffer->mem->bo;
   struct v3dv_job *csd_job = info->csd_job;

   struct drm_v3d_submit_cpu submit = {0};

   submit.bo_handle_count = 1;
   submit.bo_handles = (uintptr_t)(void *)&bo->handle;

   uint32_t *bo_handles = (uint32_t *) malloc(sizeof(uint32_t) * csd_job->bo_count);
   uint32_t bo_idx = 0;
   set_foreach (csd_job->bos, entry) {
      struct v3dv_bo *csd_bo = (struct v3dv_bo *)entry->key;
      /* dedup against cpu_job bo */
      if (csd_bo->handle == bo->handle)
         continue;
      bo_handles[bo_idx++] = csd_bo->handle;
   }
   csd_job->csd.submit.bo_handle_count = bo_idx;
   csd_job->csd.submit.bo_handles = (uintptr_t)(void *)bo_handles;

   struct drm_v3d_indirect_csd indirect = {0};

   v3d_submit_ext_set(&indirect.base, NULL, DRM_V3D_EXT_ID_CPU_INDIRECT_CSD, 0);

   indirect.submit = csd_job->csd.submit;
   indirect.offset = info->buffer->mem_offset + info->offset;
   indirect.wg_size = info->wg_size;

   for (int i = 0; i < 3; i++) {
      if (info->wg_uniform_offsets[i]) {
         assert(info->wg_uniform_offsets[i] >= (uint32_t *) csd_job->indirect.base);
         indirect.wg_uniform_offsets[i] = info->wg_uniform_offsets[i] - (uint32_t *) csd_job->indirect.base;
      } else {
         indirect.wg_uniform_offsets[i] = 0xffffffff; /* No rewrite */
      }
   }

   indirect.indirect = csd_job->indirect.bo->handle;

   struct v3d_multisync ms = {0};

   /* We need to configure the semaphores of this job with the indirect
    * CSD job, as the CPU job must obey to the CSD job synchronization
    * demands, such as barriers.
    */
   if (!set_multisync(&ms, sync_info, NULL, 0, (void *)&indirect, queue, csd_job,
                     V3DV_QUEUE_CPU, V3DV_QUEUE_CSD, V3D_CPU)) {
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
   submit.extensions = (uintptr_t)(void *)&ms.ext;

   int ret = v3d_ioctl(device->pdevice->render_fd,
			DRM_IOCTL_V3D_SUBMIT_CPU, &submit);

   free(bo_handles);
   v3d_multisync_free(&ms);

   queue->last_job_syncs.first[V3DV_QUEUE_CPU] = false;
   queue->last_job_syncs.first[V3DV_QUEUE_CSD] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CPU failed: %m");

   return VK_SUCCESS;
}

static inline void
job_add_device_address_bos(struct v3dv_job *job, struct v3dv_queue *queue)
{
   if (!job->uses_buffer_device_address)
      return;

   struct v3dv_device *device = queue->device;

   mtx_lock(&device->queue_mutex);
   util_dynarray_foreach(&device->device_address_bo_list,
                         struct v3dv_bo *, bo) {
      v3dv_job_add_bo(job, *bo);
   }
   mtx_unlock(&device->queue_mutex);
}

static VkResult
handle_cl_job(struct v3dv_queue *queue,
              struct v3dv_job *job,
              uint32_t counter_pass_idx,
              struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   struct v3dv_device *device = queue->device;

   struct drm_v3d_submit_cl submit = { 0 };

   /* Sanity check: we should only flag a bcl sync on a job that needs to be
    * serialized.
    */
   assert(job->serialize || !job->needs_bcl_sync);

   /* We expect to have just one RCL per job which should fit in just one BO.
    * Our BCL, could chain multiple BOS together though.
    */
   assert(list_length(&job->rcl.bo_list) == 1);
   assert(list_length(&job->bcl.bo_list) >= 1);
   struct v3dv_bo *bcl_fist_bo =
      list_first_entry(&job->bcl.bo_list, struct v3dv_bo, list_link);
   submit.bcl_start = bcl_fist_bo->offset;
   submit.bcl_end = job->suspending ? job->suspended_bcl_end :
                                      job->bcl.bo->offset + v3dv_cl_offset(&job->bcl);
   submit.rcl_start = job->rcl.bo->offset;
   submit.rcl_end = job->rcl.bo->offset + v3dv_cl_offset(&job->rcl);

   submit.qma = job->tile_alloc->offset;
   submit.qms = job->tile_alloc->size;
   submit.qts = job->tile_state->offset;

   submit.flags = 0;
   if (job->tmu_dirty_rcl)
      submit.flags |= DRM_V3D_SUBMIT_CL_FLUSH_CACHE;

   /* If the job uses VK_KHR_buffer_device_address we need to ensure all
    * buffers flagged with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    * are included.
    */
   job_add_device_address_bos(job, queue);

   submit.bo_handle_count = job->bo_count;
   uint32_t *bo_handles =
      (uint32_t *) malloc(sizeof(uint32_t) * submit.bo_handle_count);
   uint32_t bo_idx = 0;
   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (struct v3dv_bo *)entry->key;
      bo_handles[bo_idx++] = bo->handle;
   }
   assert(bo_idx == submit.bo_handle_count);
   submit.bo_handles = (uintptr_t)(void *)bo_handles;

   submit.perfmon_id = job->perf ?
      job->perf->kperfmon_ids[counter_pass_idx] : 0;
   mtx_lock(&device->queue_mutex);
   const bool needs_perf_sync = device->last_perfmon_id != submit.perfmon_id;
   device->last_perfmon_id = submit.perfmon_id;
   mtx_unlock(&device->queue_mutex);

   /* We need a binning sync if we are the first CL job waiting on a semaphore
    * with a wait stage that involves the geometry pipeline, or if the job
    * comes after a pipeline barrier that involves geometry stages
    * (needs_bcl_sync) or when performance queries are in use.
    *
    * We need a render sync if the job doesn't need a binning sync but has
    * still been flagged for serialization. It should be noted that RCL jobs
    * don't start until the previous RCL job has finished so we don't really
    * need to add a fence for those, however, we might need to wait on a CSD or
    * TFU job, which are not automatically serialized with CL jobs.
    */
   bool needs_bcl_sync = job->needs_bcl_sync || needs_perf_sync;
   if (queue->last_job_syncs.first[V3DV_QUEUE_CL]) {
      for (int i = 0; !needs_bcl_sync && i < sync_info->wait_count; i++) {
         needs_bcl_sync = sync_info->waits[i].stage_mask &
             (VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
              VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT |
              VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
              VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
              VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
              VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
              VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
              VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
              VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
              VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
              VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT);
      }
   }

   bool needs_rcl_sync = job->serialize && !needs_bcl_sync;

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphores extension.
    */
   struct v3d_multisync ms = { 0 };
   enum v3d_queue wait_stage = needs_rcl_sync ? V3D_RENDER : V3D_BIN;
   if (!set_multisync(&ms, sync_info, NULL, 0, NULL, queue, job,
                     V3DV_QUEUE_CL, V3DV_QUEUE_CL, wait_stage)) {
      free(bo_handles);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
   submit.extensions = (uintptr_t)(void *)&ms.ext;

   /* We are using multisync so disable legacy single-sync interface */
   submit.in_sync_rcl = 0;
   submit.in_sync_bcl = 0;
   submit.out_sync = 0;

   v3dv_clif_dump(device, job, &submit);
   int ret = v3d_ioctl(device->pdevice->render_fd,
                       DRM_IOCTL_V3D_SUBMIT_CL, &submit);

   static bool warned = false;
   if (ret && !warned) {
      mesa_loge("Draw call returned %s. Expect corruption.\n",
                strerror(errno));
      warned = true;
   }

   free(bo_handles);
   v3d_multisync_free(&ms);

   queue->last_job_syncs.first[V3DV_QUEUE_CL] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CL failed: %m");

   return VK_SUCCESS;
}

static VkResult
handle_tfu_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   assert(!V3D_DBG(DISABLE_TFU));

   struct v3dv_device *device = queue->device;

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphore extension.
    */
   struct v3d_multisync ms = { 0 };
   if (!set_multisync(&ms, sync_info, NULL, 0, NULL, queue, job,
                     V3DV_QUEUE_TFU, V3DV_QUEUE_TFU, V3D_TFU)) {
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   job->tfu.flags |= DRM_V3D_SUBMIT_EXTENSION;
   job->tfu.extensions = (uintptr_t)(void *)&ms.ext;

   /* We are using multisync so disable legacy single-sync interface */
   job->tfu.in_sync = 0;
   job->tfu.out_sync = 0;

   int ret = v3d_ioctl(device->pdevice->render_fd,
                       DRM_IOCTL_V3D_SUBMIT_TFU, &job->tfu);

   v3d_multisync_free(&ms);
   queue->last_job_syncs.first[V3DV_QUEUE_TFU] = false;

   if (ret != 0)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_TFU failed: %m");

   return VK_SUCCESS;
}

static VkResult
handle_csd_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               uint32_t counter_pass_idx,
               struct v3dv_submit_sync_info *sync_info)
{
   MESA_TRACE_FUNC();
   struct v3dv_device *device = queue->device;

   struct drm_v3d_submit_csd *submit = &job->csd.submit;

   /* If the job uses VK_KHR_buffer_device_address we need to ensure all
    * buffers flagged with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    * are included.
    */
   job_add_device_address_bos(job, queue);

   submit->bo_handle_count = job->bo_count;
   uint32_t *bo_handles =
      (uint32_t *) malloc(sizeof(uint32_t) * MAX2(4, submit->bo_handle_count * 2));
   uint32_t bo_idx = 0;
   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (struct v3dv_bo *)entry->key;
      bo_handles[bo_idx++] = bo->handle;
   }
   assert(bo_idx == submit->bo_handle_count);
   submit->bo_handles = (uintptr_t)(void *)bo_handles;

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphore extension.
    */
   struct v3d_multisync ms = { 0 };
   if (!set_multisync(&ms, sync_info, NULL, 0, NULL, queue, job,
                     V3DV_QUEUE_CSD, V3DV_QUEUE_CSD, V3D_CSD)) {
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }
   submit->flags |= DRM_V3D_SUBMIT_EXTENSION;
   submit->extensions = (uintptr_t)(void *)&ms.ext;

   /* We are using multisync so disable legacy single-sync interface */
   submit->in_sync = 0;
   submit->out_sync = 0;

   submit->perfmon_id = job->perf ?
      job->perf->kperfmon_ids[counter_pass_idx] : 0;
   mtx_lock(&device->queue_mutex);
   device->last_perfmon_id = submit->perfmon_id;
   mtx_unlock(&device->queue_mutex);

   int ret = v3d_ioctl(device->pdevice->render_fd,
                       DRM_IOCTL_V3D_SUBMIT_CSD, submit);

   static bool warned = false;
   if (ret && !warned) {
      mesa_loge("Compute dispatch returned %s. Expect corruption.\n",
                strerror(errno));
      warned = true;
   }

   free(bo_handles);

   v3d_multisync_free(&ms);
   queue->last_job_syncs.first[V3DV_QUEUE_CSD] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CSD failed: %m");

   return VK_SUCCESS;
}

static void
queue_apply_barrier_state(struct v3dv_job *job,
                          struct v3dv_barrier_state *barrier)
{
   if (!v3dv_job_apply_barrier_state(job, barrier))
      return;

   if (job->type != V3DV_JOB_TYPE_GPU_CL)
      return;

   if (job->serialize &&
       (barrier->bcl_buffer_access || barrier->bcl_image_access)) {
      job->needs_bcl_sync = true;
      barrier->bcl_buffer_access = barrier->bcl_image_access = 0;
   }
}

static VkResult
queue_handle_job(struct v3dv_queue *queue,
                 struct v3dv_job *job,
                 uint32_t counter_pass_idx,
                 struct v3dv_barrier_state *barrier,
                 struct v3dv_submit_sync_info *sync_info)
{
   if (barrier)
      queue_apply_barrier_state(job, barrier);

   if (unlikely(V3D_DBG(SYNC))) {
      job->serialize = V3DV_BARRIER_ALL;
      job->needs_bcl_sync = job->type == V3DV_JOB_TYPE_GPU_CL;
   }

   switch (job->type) {
   case V3DV_JOB_TYPE_GPU_CL:
      return handle_cl_job(queue, job, counter_pass_idx, sync_info);
   case V3DV_JOB_TYPE_GPU_TFU:
      return handle_tfu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_GPU_CSD:
      return handle_csd_job(queue, job, counter_pass_idx, sync_info);
   case V3DV_JOB_TYPE_CPU_RESET_QUERIES:
      return handle_reset_query_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_END_QUERY:
      return handle_end_query_cpu_job(queue, job, counter_pass_idx);
   case V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS:
      return handle_copy_query_results_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_CSD_INDIRECT:
      return handle_csd_indirect_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY:
      return handle_timestamp_query_cpu_job(queue, job, sync_info);
   default:
      UNREACHABLE("Unhandled job type");
   }
}

/* Merges an array of syncobj handles into a single target syncobj */
static VkResult
merge_syncobjs(struct v3dv_device *device,
                    const uint32_t *src_syncobjs,
                    uint32_t src_count,
                    uint32_t dst_syncobj)
{
   int drm_fd = device->pdevice->render_fd;
   int accum_fd = -1;

   if (src_count == 0) {
      /* If there are no jobs and no wait dependencies, the signal semaphores/fences
       * must trigger immediately to satisfy the Vulkan spec.
       */
      drmSyncobjSignal(drm_fd, &dst_syncobj, 1);
      return VK_SUCCESS;
   }

   for (uint32_t i = 0; i < src_count; i++) {
      int queue_fd = -1;
      if (drmSyncobjExportSyncFile(drm_fd, src_syncobjs[i], &queue_fd) != 0)
         goto ioctl_error;

      if (accum_fd == -1) {
         accum_fd = queue_fd;
      } else {
         int new_accum_fd = sync_merge("v3dv_merged_fence", accum_fd, queue_fd);
         close(queue_fd);

         if (new_accum_fd < 0)
            goto ioctl_error;

         close(accum_fd);
         accum_fd = new_accum_fd;
      }
   }

   /* Import the final accumulated fence fd back into the destination syncobj */
   assert(accum_fd != -1);
   if (drmSyncobjImportSyncFile(drm_fd, dst_syncobj, accum_fd) != 0)
      goto ioctl_error;

   close(accum_fd);

   return VK_SUCCESS;

ioctl_error:
   if (accum_fd != -1)
      close(accum_fd);
   return VK_ERROR_DEVICE_LOST;
}

VkResult
v3dv_queue_driver_submit(struct vk_queue *vk_queue,
                         struct vk_queue_submit *submit)
{
   MESA_TRACE_FUNC();
   struct v3dv_queue *queue = container_of(vk_queue, struct v3dv_queue, vk);
   VkResult result;

   struct v3dv_submit_sync_info sync_info = {
      .wait_count = submit->wait_count,
      .waits = submit->waits,
      .signal_count = submit->signal_count,
      .signals = submit->signals,
   };

   for (int i = 0; i < V3DV_QUEUE_COUNT; i++)
      queue->last_job_syncs.first[i] = true;

   struct v3dv_barrier_state pending_barrier = { 0 };
   struct v3dv_job *first_suspend_job = NULL;
   struct v3dv_job *current_suspend_job = NULL;
   bool has_jobs = false;
   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct v3dv_cmd_buffer *cmd_buffer =
         container_of(submit->command_buffers[i], struct v3dv_cmd_buffer, vk);
      list_for_each_entry_safe(struct v3dv_job, job,
                               &cmd_buffer->jobs, list_link) {
         if (job->suspending) {
            job = v3d_X((&job->device->devinfo),
                         cmd_buffer_prepare_suspend_job_for_submit)(job);
            if (!job)
               return VK_ERROR_OUT_OF_DEVICE_MEMORY;
         }

         if (job->suspending && !job->resuming) {
            assert(!first_suspend_job);
            assert(!current_suspend_job);
            first_suspend_job = job;
         }

         if (job->resuming) {
            assert(first_suspend_job);
            assert(current_suspend_job);
            v3d_X((&job->device->devinfo), job_patch_resume_address)(first_suspend_job,
                                                          current_suspend_job,
                                                          job);
            current_suspend_job = NULL;
         }

         if (job->suspending) {
            current_suspend_job = job;
         } else {
            assert(!current_suspend_job);
            struct v3dv_job *submit_job = first_suspend_job ?
                                          first_suspend_job : job;
            result =
               queue_handle_job(queue, submit_job, submit->perf_pass_index,
                                &pending_barrier, &sync_info);

            if (result != VK_SUCCESS)
               return result;

            has_jobs = true;
            first_suspend_job = NULL;
         }
      }

      /* If the command buffer ends with a barrier, save the pending barrier
       * state so we can apply it on the next command buffer.
       */
      v3dv_merge_barrier_state(&pending_barrier, &cmd_buffer->state.barrier);
   }

   assert(!first_suspend_job);
   assert(!current_suspend_job);

   /* Handle signaling now */
   if (submit->signal_count > 0) {
      /* We need to signal after all work (including wait dependencies)
       * has completed. Vulkan also requires that submissions to the
       * same queue signal in order. To ensure this, we accumulate the
       * last_sync of all kernel queues into the signals of the command
       * buffer regardless of whether we submitted any jobs to them in this
       * command buffer (this satisfies the Vulkan signaling order
       * requirement), so these are signaled as soon as all the queues
       * finish. If the command buffer didn't submit any jobs, then we
       * also need to gate signaling on the wait dependencies.
       */
      uint32_t src_syncobjs[V3DV_QUEUE_COUNT + submit->wait_count];
      uint32_t src_count = 0;

      for (int i = 0; i < V3DV_QUEUE_COUNT; i++)
         src_syncobjs[src_count++] = queue->last_job_syncs.syncs[i];

      /* If we didn't submit any jobs, we need to merge wait dependencies */
      if (!has_jobs && submit->wait_count > 0) {
         for (uint32_t i = 0; i < submit->wait_count; i++)
            src_syncobjs[src_count++] = vk_sync_as_drm_syncobj(submit->waits[i].sync)->syncobj;
      }

      /* Merge the accumulated syncobjs into each Vulkan signal semaphore */
      for (uint32_t i = 0; i < submit->signal_count; i++) {
         uint32_t dst_syncobj = vk_sync_as_drm_syncobj(submit->signals[i].sync)->syncobj;

         result = merge_syncobjs(queue->device, src_syncobjs, src_count, dst_syncobj);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_QueueBindSparse(VkQueue _queue,
                     uint32_t bindInfoCount,
                     const VkBindSparseInfo *pBindInfo,
                     VkFence fence)
{
   V3DV_FROM_HANDLE(v3dv_queue, queue, _queue);
   return vk_error(queue, VK_ERROR_FEATURE_NOT_PRESENT);
}
