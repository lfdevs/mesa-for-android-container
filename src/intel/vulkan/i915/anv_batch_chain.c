/*
 * Copyright © 2022 Intel Corporation
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

#include "i915/anv_batch_chain.h"
#include "anv_private.h"
#include "anv_measure.h"

#include "perf/intel_perf.h"
#include "util/u_debug.h"

#include "drm-uapi/i915_drm.h"

struct anv_execbuf {
   struct drm_i915_gem_execbuffer2           execbuf;

   struct drm_i915_gem_execbuffer_ext_timeline_fences timeline_fences;

   struct drm_i915_gem_exec_object2 *        objects;
   uint32_t                                  bo_count;
   uint32_t                                  bo_array_length;
   struct anv_bo **                          bos;

   uint32_t                                  syncobj_count;
   uint32_t                                  syncobj_array_length;
   struct drm_i915_gem_exec_fence *          syncobjs;
   uint64_t *                                syncobj_values;

   uint32_t                                  cmd_buffer_count;
   struct anv_query_pool                     *perf_query_pool;

   const VkAllocationCallbacks *             alloc;
   VkSystemAllocationScope                   alloc_scope;

   int                                       perf_query_pass;
};

static void
anv_execbuf_finish(struct anv_execbuf *exec)
{
   vk_free(exec->alloc, exec->syncobjs);
   vk_free(exec->alloc, exec->syncobj_values);
   vk_free(exec->alloc, exec->objects);
   vk_free(exec->alloc, exec->bos);
}

static void
anv_execbuf_add_ext(struct anv_execbuf *exec,
                    uint32_t ext_name,
                    struct i915_user_extension *ext)
{
   __u64 *iter = &exec->execbuf.cliprects_ptr;

   exec->execbuf.flags |= I915_EXEC_USE_EXTENSIONS;

   while (*iter != 0) {
      iter = (__u64 *) &((struct i915_user_extension *)(uintptr_t)*iter)->next_extension;
   }

   ext->name = ext_name;

   *iter = (uintptr_t) ext;
}

static VkResult
anv_execbuf_add_bo_bitset(struct anv_device *device,
                          struct anv_execbuf *exec,
                          uint32_t dep_words,
                          BITSET_WORD *deps,
                          uint32_t extra_flags);

static VkResult
anv_execbuf_add_bo(struct anv_device *device,
                   struct anv_execbuf *exec,
                   struct anv_bo *bo,
                   struct anv_reloc_list *relocs,
                   uint32_t extra_flags)
{
   struct drm_i915_gem_exec_object2 *obj = NULL;

   if (bo->exec_obj_index < exec->bo_count &&
       exec->bos[bo->exec_obj_index] == bo)
      obj = &exec->objects[bo->exec_obj_index];

   if (obj == NULL) {
      /* We've never seen this one before.  Add it to the list and assign
       * an id that we can use later.
       */
      if (exec->bo_count >= exec->bo_array_length) {
         uint32_t new_len = exec->objects ? exec->bo_array_length * 2 : 64;

         struct drm_i915_gem_exec_object2 *new_objects =
            vk_realloc(exec->alloc, exec->objects,
                       new_len * sizeof(*new_objects), 8, exec->alloc_scope);
         if (new_objects == NULL)
            return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

         exec->objects = new_objects;

         struct anv_bo **new_bos =
            vk_realloc(exec->alloc, exec->bos, new_len * sizeof(*new_bos), 8,
                       exec->alloc_scope);
         if (new_bos == NULL)
            return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

         exec->bos = new_bos;
         exec->bo_array_length = new_len;
      }

      assert(exec->bo_count < exec->bo_array_length);

      bo->exec_obj_index = exec->bo_count++;
      obj = &exec->objects[bo->exec_obj_index];
      exec->bos[bo->exec_obj_index] = bo;

      obj->handle = bo->gem_handle;
      obj->relocation_count = 0;
      obj->relocs_ptr = 0;
      obj->alignment = 0;
      obj->offset = bo->offset;
      obj->flags = bo->flags | extra_flags;
      obj->rsvd1 = 0;
      obj->rsvd2 = 0;
   }

   if (extra_flags & EXEC_OBJECT_WRITE) {
      obj->flags |= EXEC_OBJECT_WRITE;
      obj->flags &= ~EXEC_OBJECT_ASYNC;
   }

   if (relocs != NULL) {
      return anv_execbuf_add_bo_bitset(device, exec, relocs->dep_words,
                                       relocs->deps, extra_flags);
   }

   return VK_SUCCESS;
}

/* Add BO dependencies to execbuf */
static VkResult
anv_execbuf_add_bo_bitset(struct anv_device *device,
                          struct anv_execbuf *exec,
                          uint32_t dep_words,
                          BITSET_WORD *deps,
                          uint32_t extra_flags)
{
   for (uint32_t w = 0; w < dep_words; w++) {
      BITSET_WORD mask = deps[w];
      while (mask) {
         int i = u_bit_scan(&mask);
         uint32_t gem_handle = w * BITSET_WORDBITS + i;
         struct anv_bo *bo = anv_device_lookup_bo(device, gem_handle);
         assert(bo->refcount > 0);
         VkResult result =
            anv_execbuf_add_bo(device, exec, bo, NULL, extra_flags);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}

static VkResult
anv_execbuf_add_syncobj(struct anv_device *device,
                        struct anv_execbuf *exec,
                        uint32_t syncobj,
                        uint32_t flags,
                        uint64_t timeline_value)
{
   if (exec->syncobj_count >= exec->syncobj_array_length) {
      uint32_t new_len = MAX2(exec->syncobj_array_length * 2, 16);

      struct drm_i915_gem_exec_fence *new_syncobjs =
         vk_realloc(exec->alloc, exec->syncobjs,
                    new_len * sizeof(*new_syncobjs), 8, exec->alloc_scope);
      if (new_syncobjs == NULL)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      exec->syncobjs = new_syncobjs;

      if (exec->syncobj_values) {
         uint64_t *new_syncobj_values =
            vk_realloc(exec->alloc, exec->syncobj_values,
                       new_len * sizeof(*new_syncobj_values), 8,
                       exec->alloc_scope);
         if (new_syncobj_values == NULL)
            return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

         exec->syncobj_values = new_syncobj_values;
      }

      exec->syncobj_array_length = new_len;
   }

   if (timeline_value && !exec->syncobj_values) {
      exec->syncobj_values =
         vk_zalloc(exec->alloc, exec->syncobj_array_length *
                                sizeof(*exec->syncobj_values),
                   8, exec->alloc_scope);
      if (!exec->syncobj_values)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   exec->syncobjs[exec->syncobj_count] = (struct drm_i915_gem_exec_fence) {
      .handle = syncobj,
      .flags = flags,
   };
   if (exec->syncobj_values)
      exec->syncobj_values[exec->syncobj_count] = timeline_value;

   exec->syncobj_count++;

   return VK_SUCCESS;
}

static VkResult
anv_execbuf_add_sync(struct anv_device *device,
                     struct anv_execbuf *execbuf,
                     struct vk_sync *sync,
                     bool is_signal,
                     uint64_t value)
{
   /* It's illegal to signal a timeline with value 0 because that's never
    * higher than the current value.  A timeline wait on value 0 is always
    * trivial because 0 <= uint64_t always.
    */
   if ((sync->flags & VK_SYNC_IS_TIMELINE) && value == 0)
      return VK_SUCCESS;

   if (vk_sync_is_anv_bo_sync(sync)) {
      struct anv_bo_sync *bo_sync =
         container_of(sync, struct anv_bo_sync, sync);

      assert(is_signal == (bo_sync->state == ANV_BO_SYNC_STATE_RESET));

      return anv_execbuf_add_bo(device, execbuf, bo_sync->bo, NULL,
                                is_signal ? EXEC_OBJECT_WRITE : 0);
   } else if (vk_sync_type_is_drm_syncobj(sync->type)) {
      struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(sync);

      if (!(sync->flags & VK_SYNC_IS_TIMELINE))
         value = 0;

      return anv_execbuf_add_syncobj(device, execbuf, syncobj->syncobj,
                                     is_signal ? I915_EXEC_FENCE_SIGNAL :
                                                 I915_EXEC_FENCE_WAIT,
                                     value);
   }

   unreachable("Invalid sync type");
}

static VkResult
setup_execbuf_for_cmd_buffer(struct anv_execbuf *execbuf,
                             struct anv_cmd_buffer *cmd_buffer)
{
   VkResult result;
   /* Add surface dependencies (BOs) to the execbuf */
   result = anv_execbuf_add_bo_bitset(cmd_buffer->device, execbuf,
                                      cmd_buffer->surface_relocs.dep_words,
                                      cmd_buffer->surface_relocs.deps, 0);
   if (result != VK_SUCCESS)
      return result;

   /* First, we walk over all of the bos we've seen and add them and their
    * relocations to the validate list.
    */
   struct anv_batch_bo **bbo;
   u_vector_foreach(bbo, &cmd_buffer->seen_bbos) {
      result = anv_execbuf_add_bo(cmd_buffer->device, execbuf,
                                  (*bbo)->bo, &(*bbo)->relocs, 0);
      if (result != VK_SUCCESS)
         return result;
   }

   struct anv_bo **bo_entry;
   u_vector_foreach(bo_entry, &cmd_buffer->dynamic_bos) {
      result = anv_execbuf_add_bo(cmd_buffer->device, execbuf,
                                  *bo_entry, NULL, 0);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
pin_state_pool(struct anv_device *device,
               struct anv_execbuf *execbuf,
               struct anv_state_pool *pool)
{
   anv_block_pool_foreach_bo(bo, &pool->block_pool) {
      VkResult result = anv_execbuf_add_bo(device, execbuf, bo, NULL, 0);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
setup_execbuf_for_cmd_buffers(struct anv_execbuf *execbuf,
                              struct anv_queue *queue,
                              struct anv_cmd_buffer **cmd_buffers,
                              uint32_t num_cmd_buffers)
{
   struct anv_device *device = queue->device;
   VkResult result;

   /* Edit the tail of the command buffers to chain them all together if they
    * can be.
    */
   anv_cmd_buffer_chain_command_buffers(cmd_buffers, num_cmd_buffers);

   for (uint32_t i = 0; i < num_cmd_buffers; i++) {
      anv_measure_submit(cmd_buffers[i]);
      result = setup_execbuf_for_cmd_buffer(execbuf, cmd_buffers[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   /* Add all the global BOs to the object list for softpin case. */
   result = pin_state_pool(device, execbuf, &device->scratch_surface_state_pool);
   if (result != VK_SUCCESS)
      return result;

   result = pin_state_pool(device, execbuf, &device->bindless_surface_state_pool);
   if (result != VK_SUCCESS)
      return result;

   result = pin_state_pool(device, execbuf, &device->internal_surface_state_pool);
   if (result != VK_SUCCESS)
      return result;

   result = pin_state_pool(device, execbuf, &device->dynamic_state_pool);
   if (result != VK_SUCCESS)
      return result;

   result = pin_state_pool(device, execbuf, &device->general_state_pool);
   if (result != VK_SUCCESS)
      return result;

   result = pin_state_pool(device, execbuf, &device->instruction_state_pool);
   if (result != VK_SUCCESS)
      return result;

   result = pin_state_pool(device, execbuf, &device->binding_table_pool);
   if (result != VK_SUCCESS)
      return result;

   /* Add the BOs for all user allocated memory objects because we can't
    * track after binding updates of VK_EXT_descriptor_indexing.
    */
   list_for_each_entry(struct anv_device_memory, mem,
                       &device->memory_objects, link) {
      result = anv_execbuf_add_bo(device, execbuf, mem->bo, NULL, 0);
      if (result != VK_SUCCESS)
         return result;
   }

   /* Add all the private BOs from images because we can't track after binding
    * updates of VK_EXT_descriptor_indexing.
    */
   list_for_each_entry(struct anv_image, image,
                       &device->image_private_objects, link) {
      struct anv_bo *private_bo =
         image->bindings[ANV_IMAGE_MEMORY_BINDING_PRIVATE].address.bo;
      result = anv_execbuf_add_bo(device, execbuf, private_bo, NULL, 0);
      if (result != VK_SUCCESS)
         return result;
   }

   struct anv_batch_bo *first_batch_bo =
      list_first_entry(&cmd_buffers[0]->batch_bos, struct anv_batch_bo, link);

   /* The kernel requires that the last entry in the validation list be the
    * batch buffer to execute.  We can simply swap the element
    * corresponding to the first batch_bo in the chain with the last
    * element in the list.
    */
   if (first_batch_bo->bo->exec_obj_index != execbuf->bo_count - 1) {
      uint32_t idx = first_batch_bo->bo->exec_obj_index;
      uint32_t last_idx = execbuf->bo_count - 1;

      struct drm_i915_gem_exec_object2 tmp_obj = execbuf->objects[idx];
      assert(execbuf->bos[idx] == first_batch_bo->bo);

      execbuf->objects[idx] = execbuf->objects[last_idx];
      execbuf->bos[idx] = execbuf->bos[last_idx];
      execbuf->bos[idx]->exec_obj_index = idx;

      execbuf->objects[last_idx] = tmp_obj;
      execbuf->bos[last_idx] = first_batch_bo->bo;
      first_batch_bo->bo->exec_obj_index = last_idx;
   }

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   if (device->physical->memory.need_clflush)
      anv_cmd_buffer_clflush(cmd_buffers, num_cmd_buffers);
#endif

   execbuf->execbuf = (struct drm_i915_gem_execbuffer2) {
      .buffers_ptr = (uintptr_t) execbuf->objects,
      .buffer_count = execbuf->bo_count,
      .batch_start_offset = 0,
      /* We'll fill in batch length later when chaining batches. */
      .batch_len = 0,
      .cliprects_ptr = 0,
      .num_cliprects = 0,
      .DR1 = 0,
      .DR4 = 0,
      .flags = I915_EXEC_NO_RELOC |
               I915_EXEC_HANDLE_LUT |
               queue->exec_flags,
      .rsvd1 = device->context_id,
      .rsvd2 = 0,
   };

   return VK_SUCCESS;
}

static VkResult
setup_empty_execbuf(struct anv_execbuf *execbuf, struct anv_queue *queue)
{
   struct anv_device *device = queue->device;
   VkResult result = anv_execbuf_add_bo(device, execbuf,
                                        device->trivial_batch_bo,
                                        NULL, 0);
   if (result != VK_SUCCESS)
      return result;

   execbuf->execbuf = (struct drm_i915_gem_execbuffer2) {
      .buffers_ptr = (uintptr_t) execbuf->objects,
      .buffer_count = execbuf->bo_count,
      .batch_start_offset = 0,
      .batch_len = 8, /* GFX7_MI_BATCH_BUFFER_END and NOOP */
      .flags = I915_EXEC_HANDLE_LUT | queue->exec_flags | I915_EXEC_NO_RELOC,
      .rsvd1 = device->context_id,
      .rsvd2 = 0,
   };

   return VK_SUCCESS;
}

static VkResult
setup_utrace_execbuf(struct anv_execbuf *execbuf, struct anv_queue *queue,
                     struct anv_utrace_submit *submit)
{
   struct anv_device *device = queue->device;

   /* Always add the workaround BO as it includes a driver identifier for the
    * error_state.
    */
   VkResult result = anv_execbuf_add_bo(device, execbuf,
                                        device->workaround_bo,
                                        NULL, 0);
   if (result != VK_SUCCESS)
      return result;

   result = anv_execbuf_add_bo(device, execbuf,
                               submit->batch_bo,
                               &submit->relocs, 0);
   if (result != VK_SUCCESS)
      return result;

   result = anv_execbuf_add_sync(device, execbuf, submit->sync,
                                 true /* is_signal */, 0 /* value */);
   if (result != VK_SUCCESS)
      return result;

   if (submit->batch_bo->exec_obj_index != execbuf->bo_count - 1) {
      uint32_t idx = submit->batch_bo->exec_obj_index;
      uint32_t last_idx = execbuf->bo_count - 1;

      struct drm_i915_gem_exec_object2 tmp_obj = execbuf->objects[idx];
      assert(execbuf->bos[idx] == submit->batch_bo);

      execbuf->objects[idx] = execbuf->objects[last_idx];
      execbuf->bos[idx] = execbuf->bos[last_idx];
      execbuf->bos[idx]->exec_obj_index = idx;

      execbuf->objects[last_idx] = tmp_obj;
      execbuf->bos[last_idx] = submit->batch_bo;
      submit->batch_bo->exec_obj_index = last_idx;
   }

#ifdef SUPPORT_INTEL_INTEGRATED_GPUS
   if (device->physical->memory.need_clflush)
      intel_flush_range(submit->batch_bo->map, submit->batch_bo->size);
#endif

   execbuf->execbuf = (struct drm_i915_gem_execbuffer2) {
      .buffers_ptr = (uintptr_t) execbuf->objects,
      .buffer_count = execbuf->bo_count,
      .batch_start_offset = 0,
      .batch_len = submit->batch.next - submit->batch.start,
      .flags = I915_EXEC_NO_RELOC |
               I915_EXEC_HANDLE_LUT |
               I915_EXEC_FENCE_ARRAY |
               queue->exec_flags,
      .rsvd1 = device->context_id,
      .rsvd2 = 0,
      .num_cliprects = execbuf->syncobj_count,
      .cliprects_ptr = (uintptr_t)execbuf->syncobjs,
   };

   return VK_SUCCESS;
}

static int
anv_gem_execbuffer(struct anv_device *device,
                   struct drm_i915_gem_execbuffer2 *execbuf)
{
   if (execbuf->flags & I915_EXEC_FENCE_OUT)
      return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2_WR, execbuf);
   else
      return intel_ioctl(device->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, execbuf);
}

static VkResult
anv_queue_exec_utrace_locked(struct anv_queue *queue,
                             struct anv_utrace_submit *submit)
{
   assert(submit->batch_bo);

   struct anv_device *device = queue->device;
   struct anv_execbuf execbuf = {
      .alloc = &device->vk.alloc,
      .alloc_scope = VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
   };

   VkResult result = setup_utrace_execbuf(&execbuf, queue, submit);
   if (result != VK_SUCCESS)
      goto error;

   int ret = queue->device->info->no_hw ? 0 :
      anv_gem_execbuffer(queue->device, &execbuf.execbuf);
   if (ret)
      result = vk_queue_set_lost(&queue->vk, "execbuf2 failed: %m");

 error:
   anv_execbuf_finish(&execbuf);

   return result;
}

static void
anv_i915_debug_submit(const struct anv_execbuf *execbuf)
{
   uint32_t total_size_kb = 0, total_vram_only_size_kb = 0;
   for (uint32_t i = 0; i < execbuf->bo_count; i++) {
      const struct anv_bo *bo = execbuf->bos[i];
      total_size_kb += bo->size / 1024;
      if (bo->vram_only)
         total_vram_only_size_kb += bo->size / 1024;
   }

   fprintf(stderr, "Batch offset=0x%x len=0x%x on queue 0 (aperture: %.1fMb, %.1fMb VRAM only)\n",
           execbuf->execbuf.batch_start_offset, execbuf->execbuf.batch_len,
           (float)total_size_kb / 1024.0f,
           (float)total_vram_only_size_kb / 1024.0f);
   for (uint32_t i = 0; i < execbuf->bo_count; i++) {
      const struct anv_bo *bo = execbuf->bos[i];
      uint64_t size = bo->size + bo->_ccs_size;

      fprintf(stderr, "   BO: addr=0x%016"PRIx64"-0x%016"PRIx64" size=%7"PRIu64
              "KB handle=%05u capture=%u vram_only=%u name=%s\n",
              bo->offset, bo->offset + size - 1, size / 1024, bo->gem_handle,
              (bo->flags & EXEC_OBJECT_CAPTURE) != 0,
              bo->vram_only, bo->name);
   }
}

VkResult
i915_queue_exec_locked(struct anv_queue *queue,
                       uint32_t wait_count,
                       const struct vk_sync_wait *waits,
                       uint32_t cmd_buffer_count,
                       struct anv_cmd_buffer **cmd_buffers,
                       uint32_t signal_count,
                       const struct vk_sync_signal *signals,
                       struct anv_query_pool *perf_query_pool,
                       uint32_t perf_query_pass)
{
   struct anv_device *device = queue->device;
   struct anv_utrace_submit *utrace_submit = NULL;
   struct anv_execbuf execbuf = {
      .alloc = &queue->device->vk.alloc,
      .alloc_scope = VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
      .perf_query_pass = perf_query_pass,
   };

   /* Flush the trace points first, they need to be moved */
   VkResult result =
      anv_device_utrace_flush_cmd_buffers(queue,
                                          cmd_buffer_count,
                                          cmd_buffers,
                                          &utrace_submit);
   if (result != VK_SUCCESS)
      goto error;

   if (utrace_submit && !utrace_submit->batch_bo) {
      result = anv_execbuf_add_sync(device, &execbuf,
                                    utrace_submit->sync,
                                    true /* is_signal */,
                                    0);
      if (result != VK_SUCCESS)
         goto error;

      /* When The utrace submission doesn't have its own batch buffer*/
      utrace_submit = NULL;
   }

   /* Always add the workaround BO as it includes a driver identifier for the
    * error_state.
    */
   result =
      anv_execbuf_add_bo(device, &execbuf, device->workaround_bo, NULL, 0);
   if (result != VK_SUCCESS)
      goto error;

   for (uint32_t i = 0; i < wait_count; i++) {
      result = anv_execbuf_add_sync(device, &execbuf,
                                    waits[i].sync,
                                    false /* is_signal */,
                                    waits[i].wait_value);
      if (result != VK_SUCCESS)
         goto error;
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      result = anv_execbuf_add_sync(device, &execbuf,
                                    signals[i].sync,
                                    true /* is_signal */,
                                    signals[i].signal_value);
      if (result != VK_SUCCESS)
         goto error;
   }

   if (queue->sync) {
      result = anv_execbuf_add_sync(device, &execbuf,
                                    queue->sync,
                                    true /* is_signal */,
                                    0 /* signal_value */);
      if (result != VK_SUCCESS)
         goto error;
   }

   if (cmd_buffer_count) {
      result = setup_execbuf_for_cmd_buffers(&execbuf, queue,
                                             cmd_buffers,
                                             cmd_buffer_count);
   } else {
      result = setup_empty_execbuf(&execbuf, queue);
   }

   if (result != VK_SUCCESS)
      goto error;

   const bool has_perf_query =
      perf_query_pool && perf_query_pass >= 0 && cmd_buffer_count;

   if (INTEL_DEBUG(DEBUG_SUBMIT))
      anv_i915_debug_submit(&execbuf);

   anv_cmd_buffer_exec_batch_debug(queue, cmd_buffer_count, cmd_buffers,
                                   perf_query_pool, perf_query_pass);

   if (execbuf.syncobj_values) {
      execbuf.timeline_fences.fence_count = execbuf.syncobj_count;
      execbuf.timeline_fences.handles_ptr = (uintptr_t)execbuf.syncobjs;
      execbuf.timeline_fences.values_ptr = (uintptr_t)execbuf.syncobj_values;
      anv_execbuf_add_ext(&execbuf,
                          DRM_I915_GEM_EXECBUFFER_EXT_TIMELINE_FENCES,
                          &execbuf.timeline_fences.base);
   } else if (execbuf.syncobjs) {
      execbuf.execbuf.flags |= I915_EXEC_FENCE_ARRAY;
      execbuf.execbuf.num_cliprects = execbuf.syncobj_count;
      execbuf.execbuf.cliprects_ptr = (uintptr_t)execbuf.syncobjs;
   }

   if (has_perf_query) {
      assert(perf_query_pass < perf_query_pool->n_passes);
      struct intel_perf_query_info *query_info =
         perf_query_pool->pass_query[perf_query_pass];

      /* Some performance queries just the pipeline statistic HW, no need for
       * OA in that case, so no need to reconfigure.
       */
      if (!INTEL_DEBUG(DEBUG_NO_OACONFIG) &&
          (query_info->kind == INTEL_PERF_QUERY_TYPE_OA ||
           query_info->kind == INTEL_PERF_QUERY_TYPE_RAW)) {
         int ret = intel_ioctl(device->perf_fd, I915_PERF_IOCTL_CONFIG,
                               (void *)(uintptr_t) query_info->oa_metrics_set_id);
         if (ret < 0) {
            result = vk_device_set_lost(&device->vk,
                                        "i915-perf config failed: %s",
                                        strerror(errno));
         }
      }

      struct anv_bo *pass_batch_bo = perf_query_pool->bo;

      struct drm_i915_gem_exec_object2 query_pass_object = {
         .handle = pass_batch_bo->gem_handle,
         .offset = pass_batch_bo->offset,
         .flags  = pass_batch_bo->flags,
      };
      struct drm_i915_gem_execbuffer2 query_pass_execbuf = {
         .buffers_ptr = (uintptr_t) &query_pass_object,
         .buffer_count = 1,
         .batch_start_offset = khr_perf_query_preamble_offset(perf_query_pool,
                                                              perf_query_pass),
         .flags = I915_EXEC_HANDLE_LUT | queue->exec_flags,
         .rsvd1 = device->context_id,
      };

      int ret = queue->device->info->no_hw ? 0 :
         anv_gem_execbuffer(queue->device, &query_pass_execbuf);
      if (ret)
         result = vk_queue_set_lost(&queue->vk, "execbuf2 failed: %m");
   }

   int ret = queue->device->info->no_hw ? 0 :
      anv_gem_execbuffer(queue->device, &execbuf.execbuf);
   if (ret) {
      anv_i915_debug_submit(&execbuf);
      result = vk_queue_set_lost(&queue->vk, "execbuf2 failed: %m");
   }

   if (result == VK_SUCCESS && queue->sync) {
      result = vk_sync_wait(&device->vk, queue->sync, 0,
                            VK_SYNC_WAIT_COMPLETE, UINT64_MAX);
      if (result != VK_SUCCESS)
         result = vk_queue_set_lost(&queue->vk, "sync wait failed");
   }

 error:
   anv_execbuf_finish(&execbuf);

   if (result == VK_SUCCESS && utrace_submit)
      result = anv_queue_exec_utrace_locked(queue, utrace_submit);

   return result;
}

VkResult
i915_execute_simple_batch(struct anv_queue *queue, struct anv_bo *batch_bo,
                          uint32_t batch_bo_size)
{
   struct anv_device *device = queue->device;
   struct anv_execbuf execbuf = {
      .alloc = &queue->device->vk.alloc,
      .alloc_scope = VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
   };

   VkResult result = anv_execbuf_add_bo(device, &execbuf, batch_bo, NULL, 0);
   if (result != VK_SUCCESS)
      goto fail;

   execbuf.execbuf = (struct drm_i915_gem_execbuffer2) {
      .buffers_ptr = (uintptr_t) execbuf.objects,
      .buffer_count = execbuf.bo_count,
      .batch_start_offset = 0,
      .batch_len = batch_bo_size,
      .flags = I915_EXEC_HANDLE_LUT | queue->exec_flags | I915_EXEC_NO_RELOC,
      .rsvd1 = device->context_id,
      .rsvd2 = 0,
   };

   if (anv_gem_execbuffer(device, &execbuf.execbuf)) {
      result = vk_device_set_lost(&device->vk, "anv_gem_execbuffer failed: %m");
      goto fail;
   }

   result = anv_device_wait(device, batch_bo, INT64_MAX);
   if (result != VK_SUCCESS)
      result = vk_device_set_lost(&device->vk,
                                  "anv_device_wait failed: %m");

fail:
   anv_execbuf_finish(&execbuf);
   return result;
}

VkResult
i915_queue_exec_trace(struct anv_queue *queue,
                      struct anv_utrace_submit *submit)
{
   assert(submit->batch_bo);

   return anv_queue_exec_utrace_locked(queue, submit);
}
